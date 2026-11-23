#include "engine.h"
#include "ec/group.h"
#include "hash/hash160.h"
#include "hash/hash_multi.h"
#include "cpu/cpu.h"
#include "checkpoint/checkpoint.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <functional>
#include <sstream>
#include <thread>
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace ex369 {

// ---------------------------------------------------------------- TargetSet

void TargetSet::add(const uint8_t h[20]) {
    std::array<uint8_t, 20> a;
    memcpy(a.data(), h, 20);
    items_.push_back(a);
}
void TargetSet::build() {
    memset(bloom_, 0, sizeof(bloom_));
    for (auto& it : items_) {
        uint64_t k;
        memcpy(&k, it.data(), 8);
        bloom_[(k >> 3) & (BLOOM_WORDS - 1)] |= (1ULL << (k & 63));
    }
}
bool TargetSet::contains(const uint8_t h[20]) const {
    for (auto& it : items_) if (memcmp(it.data(), h, 20) == 0) return true;
    return false;
}

// ---------------------------------------------------------------- helpers

static uidx floor_pow2(uidx v) {
    if (v == 0) return 1;
    uidx p = 1;
    while ((p << 1) != 0 && (p << 1) <= v) p <<= 1;
    return p;
}

// Batched affine step: P_j += Q for j in [0,n), one inversion for the batch.
//
// Structured as exactly TWO passes over the lane array.  The obvious version
// makes four (compute dx / prefix products / invert / update), and at B=512 the
// arrays do not fit L1, so each extra pass is a full re-stream from L2.
// Fusing pass 1 with the forward product, and pass 2 with the point update,
// halves that traffic.  dx is recomputed in pass 2 rather than stored: P[j].x
// is untouched until the moment it is overwritten, so the value is identical,
// and skipping the array saves 32 bytes of read+write per lane.
#ifdef EX369_UNFUSED_EC
// The 4-pass formulation, kept only so the fused one can be A/B'd honestly.
static void batch_step_affine(PointA* P, int n, const PointA& Q, Fe* prefix) {
    static thread_local std::vector<Fe> dxv, invv;
    if ((int)dxv.size() < n) { dxv.resize(n); invv.resize(n); }
    Fe* dx = dxv.data(); Fe* inv = invv.data();
    for (int j = 0; j < n; j++) fe_sub(dx[j], Q.x, P[j].x);
    batch_inverse(inv, dx, (size_t)n);
    for (int j = 0; j < n; j++) {
        if (dx[j].is_zero()) { PointA t; point_a_add(t, P[j], Q); P[j] = t; continue; }
        Fe lam, t, nx;
        fe_sub(t, Q.y, P[j].y);
        fe_mul(lam, t, inv[j]);
        fe_sqr(nx, lam);
        fe_sub(nx, nx, P[j].x);
        fe_sub(nx, nx, Q.x);
        fe_sub(t, P[j].x, nx);
        fe_mul(t, lam, t);
        fe_sub(P[j].y, t, P[j].y);
        P[j].x = nx;
    }
}
#else
static void batch_step_affine(PointA* P, int n, const PointA& Q, Fe* prefix) {
    Fe acc(1);
    // Pass 1: prefix[j] = product of dx[0..j-1]; acc ends as the full product.
    for (int j = 0; j < n; j++) {
        Fe dx;
        fe_sub(dx, Q.x, P[j].x);
        prefix[j] = acc;
        if (!dx.is_zero()) fe_mul(acc, acc, dx);
    }
    Fe u;
    fe_inv(u, acc);                      // the single inversion for the batch

    // Pass 2 (backwards): recover 1/dx[j] and apply the addition immediately.
    for (int j = n - 1; j >= 0; j--) {
        Fe dx;
        fe_sub(dx, Q.x, P[j].x);
        if (dx.is_zero()) {              // x collision: doubling or infinity
            PointA t;
            point_a_add(t, P[j], Q);
            P[j] = t;
            continue;
        }
        Fe invdx;
        fe_mul(invdx, prefix[j], u);     // = 1 / dx[j]
        fe_mul(u, u, dx);                // strip dx[j] from the running product

        Fe lam, t, nx;
        fe_sub(t, Q.y, P[j].y);
        fe_mul(lam, t, invdx);
        fe_sqr(nx, lam);
        fe_sub(nx, nx, P[j].x);
        fe_sub(nx, nx, Q.x);
        fe_sub(t, P[j].x, nx);
        fe_mul(t, lam, t);
        fe_sub(P[j].y, t, P[j].y);
        P[j].x = nx;
    }
}
#endif

struct Scratch {
    std::vector<Fe> prefix;
    std::vector<PointA> lanes;
    std::vector<PointJ> jac;     // lane anchors before normalization
    std::vector<Fe> zs, zinv;    // their Z coordinates and inverses
    std::vector<uint8_t> pub;    // b * 33 compressed pubkeys, contiguous
    std::vector<uint8_t> h160;   // b * 20 digests
    void resize(int b) {
        prefix.resize(b); lanes.resize(b);
        jac.resize(b); zs.resize(b); zinv.resize(b);
        pub.resize((size_t)b * 33);
        h160.resize((size_t)b * 20);
    }
};

Engine::~Engine() = default;

// ---------------------------------------------------------------- prepare

bool Engine::prepare(std::string& err) {
    if (cfg_.puzzle < 0 || cfg_.threads < 0 || cfg_.threads > 1024 ||
        cfg_.batch < 1 || cfg_.batch > 65536 || cfg_.status_interval_s < 0 ||
        cfg_.status_interval_s > 86400 || cfg_.checkpoint_interval_s < 1 ||
        cfg_.checkpoint_interval_s > 86400 || cfg_.segment_size == 0 || cfg_.slice_keys == 0) {
        err = "invalid execution settings (threads 0..1024, batch 1..65536, intervals at most 86400s; sizes must be positive)";
        return false;
    }
    if (cfg_.resume && cfg_.checkpoint_path.empty()) { err = "--resume requires --checkpoint PATH"; return false; }
    if (!cfg_.simd.empty() && cfg_.simd != "scalar" && cfg_.simd != "sse" && cfg_.simd != "avx2") {
        err = "--simd must be scalar, sse or avx2"; return false;
    }
    // --- range ---
    if (cfg_.puzzle > 0) {
        if (!puzzle_range(cfg_.puzzle, ks_.range_lo, ks_.range_hi)) {
            err = "invalid puzzle number";
            return false;
        }
    } else {
        ks_.range_lo = cfg_.range_lo;
        ks_.range_hi = cfg_.range_hi;
    }
    cfg_.range_lo = ks_.range_lo;
    cfg_.range_hi = ks_.range_hi;
    U256 order;
    for (int i = 0; i < 4; i++) order.d[i] = CURVE_N[i];
    if (ks_.range_lo.is_zero() || ks_.range_hi >= order) {
        err = "range must contain valid private scalars: 1 <= LO <= HI < group order";
        return false;
    }

    // --- fixed bytes ---
    std::vector<FixedSlot> slots;
    if (!resolve_slots(cfg_.fixed, ks_.range_lo, ks_.range_hi, slots, err)) return false;
    if (!patterns_.init(cfg_.fixed, slots, cfg_.fixed_rotation, cfg_.seed, err)) return false;

    uint8_t m[32], v[32];
    patterns_.materialize(patterns_.ordinal_at(0), m, v);
    memcpy(ks_.mask, m, 32);
    memcpy(ks_.value, v, 32);
    if (!ks_.finalize(err)) return false;

    // --- targets ---
    if (cfg_.has_target) targets_.add(cfg_.target);
    for (const auto& t : cfg_.extra_targets) targets_.add(t.data());
    targets_.build();
    if (targets_.size() == 0) { err = "no target loaded"; return false; }

    // --- segmentation --------------------------------------------------
    // Invariant: a segment never straddles an outer block, so every segment is
    // one arithmetic progression and the incremental P += S path always applies.
    uidx want = floor_pow2(cfg_.segment_size ? cfg_.segment_size : 1);
    uidx inner = ks_.inner_count();
    seg_size_ = want < inner ? want : inner;
    if (seg_size_ == 0) seg_size_ = 1;

    uidx total = ks_.total_indices();
    segs_per_pattern_ = total / seg_size_ + (total % seg_size_ != 0);

    // Multi-buffer hashing backend (runtime dispatch).
    mh_ = &multihash_for(select_backend(cfg_.simd.c_str()));

    batch_ = cfg_.batch > 0 ? cfg_.batch : 256;
    if ((uidx)batch_ > seg_size_) batch_ = (int)seg_size_;
    if (batch_ < 1) batch_ = 1;
    // Keep the batch a whole number of SIMD groups so no lane is wasted.
    if (mh_->lanes > 1 && batch_ >= mh_->lanes) batch_ -= batch_ % mh_->lanes;

    threads_ = cfg_.threads > 0 ? cfg_.threads : cpu_info().physical_cores;
    if (threads_ < 1) threads_ = 1;

    uidx slice_segs = 1;
    if (cfg_.round_robin) {
        slice_segs = cfg_.slice_keys / seg_size_;
        if (slice_segs == 0) slice_segs = 1;
    } else {
        slice_segs = segs_per_pattern_;
    }

    if (!sched_.init(patterns_.count(), segs_per_pattern_, seg_size_, total,
                     cfg_.round_robin, slice_segs, cfg_.global_order, cfg_.seed, err))
        return false;

    fingerprint_ = config_fingerprint(cfg_, ks_.mask);

    // --- resume ---
    if (cfg_.resume) {
        Checkpoint cp;
        if (!cp.load(cfg_.checkpoint_path, err)) return false;
        if (cp.version == 1 && (cfg_.fixed.enabled || !ks_.range_is_aligned)) {
            err = "legacy checkpoint cannot safely resume fixed-byte or non-aligned ranges; start a new run";
            return false;
        }
        {
            if (cp.config_hash != fingerprint_) {
                err = "checkpoint belongs to a different configuration (fingerprint " +
                      cp.config_hash + " != " + fingerprint_ + ")";
                return false;
            }
            Scheduler::Progress p;
            p.watermark = cp.watermark;
            p.done_above = cp.done_above;
            if (!sched_.restore(p, err)) return false;
            uidx previous;
            if (!uidx_from_dec(cp.total_candidates, previous) || previous > UINT64_MAX) {
                err = "invalid checkpoint candidate count"; return false;
            }
            resumed_candidates_ = (uint64_t)previous;
            resumed_seconds_ = cp.elapsed_seconds;
        }
    }
    return true;
}

double Engine::space_size() const {
    if (patterns_.count() > 1 || !cfg_.fixed.enabled) {
        U256 width = ks_.range_hi - ks_.range_lo + U256(1);
        double n = 0;
        for (int i = 3; i >= 0; i--) n = std::ldexp(n,64) + (double)width.d[i];
        return n;
    }
    uidx begin=0, end=ks_.total_indices();
    while (begin<end) {
        uidx mid=begin+(end-begin)/2;
        if (ks_.scalar_at(mid)<ks_.range_lo) begin=mid+1; else end=mid;
    }
    uidx first=begin; end=ks_.total_indices();
    while (begin<end) {
        uidx mid=begin+(end-begin)/2;
        if (ks_.scalar_at(mid)<=ks_.range_hi) begin=mid+1; else end=mid;
    }
    return (double)(begin-first);
}

std::string Engine::plan_description() const {
    std::ostringstream o;
    o << ks_.describe();
    o << "plan:\n";
    o << "  patterns        : " << patterns_.count()
      << " (rotation " << order_name(cfg_.fixed_rotation) << ")\n";
    o << "  segment size    : " << uidx_to_dec(seg_size_) << " keys\n";
    o << "  segments/pattern: " << uidx_to_dec(segs_per_pattern_) << "\n";
    o << "  segment order   : " << order_name(cfg_.global_order) << "\n";
    o << "  local direction : " << direction_name(cfg_.local_dir) << "\n";
    if (cfg_.local_dir == Direction::Backward && (batch_ > 1 || threads_ > 1 || patterns_.count() > 1))
        o << "  ordering note   : reverse lane walks; no single global descending sequence\n";
    o << "  round-robin     : " << (cfg_.round_robin ? "on" : "off");
    if (cfg_.round_robin)
        o << " (slice " << uidx_to_dec(sched_.effective_slice_segs() * seg_size_) << " keys)";
    o << "\n";
    o << "  tickets         : " << sched_.total_tickets() << "\n";
    o << "  threads         : " << threads_ << "  batch lanes: " << batch_ << "\n";
    o << "  hash backend    : " << (mh_ ? mh_->name : "?")
      << " (x" << (mh_ ? mh_->lanes : 1) << ")\n";
    return o.str();
}

// ---------------------------------------------------------------- hot loop

bool Engine::process(const WorkUnit& u, const KeySpace& ks, LocalCounters& lc, Scratch& sc) {
    std::vector<PointA>& lanes = sc.lanes;
    uidx count = u.index_end - u.index_begin;
    if (count == 0) return true;

    // Clip to the range when it is not an exact sub-cube (puzzle ranges are).
    uidx i_lo = 0, i_hi = count - 1;
    if (!ks.range_is_aligned) {
        auto scalar = [&](uidx i) { return ks.scalar_at(u.index_begin + i); };
        if (scalar(i_hi) < ks.range_lo || scalar(i_lo) > ks.range_hi) {
            ++lc.outside_range_segments;
            return true;
        }
        if (scalar(i_lo) < ks.range_lo) {
            uidx a = i_lo, b = i_hi;
            while (a < b) { uidx mid = a + (b - a) / 2; if (scalar(mid) >= ks.range_lo) b = mid; else a = mid + 1; }
            i_lo = a;
        }
        if (scalar(i_hi) > ks.range_hi) {
            uidx a = i_lo, b = i_hi;
            while (a < b) { uidx mid = a + (b - a + 1) / 2; if (scalar(mid) <= ks.range_hi) a = mid; else b = mid - 1; }
            i_hi = a;
        }
        if (i_lo > i_hi) { ++lc.outside_range_segments; return true; }
        count = i_hi - i_lo + 1;
    }
    bool completed = true;

    const bool backward = (cfg_.local_dir == Direction::Backward);

    // Step point S = +stride*G (forward) or -stride*G (reverse).
    static thread_local U256 cached_stride;
    static thread_local PointA cached_S, cached_Sneg;
    static thread_local bool have_cached = false;
    if (!have_cached || !(cached_stride == ks.inner_stride)) {
        ec_mul_gen(cached_S, ks.inner_stride);
        point_a_neg(cached_Sneg, cached_S);
        cached_stride = ks.inner_stride;
        have_cached = true;
    }
    const PointA& S = backward ? cached_Sneg : cached_S;

    int B = batch_;
    if ((uidx)B > count) B = (int)count;
    uidx lane_len = count / (uidx)B;
    uidx tail = count - lane_len * (uidx)B;

    // Lane j starts at enumeration index begin + i_lo + j*lane_len, walked
    // forward; in reverse mode each lane starts at its top and walks down.
    uidx first = u.index_begin + i_lo;

    if (lane_len > 0) {
        // Scalar distance between consecutive lane starts: lane_len * stride,
        // by double-and-add (lane_len is not necessarily a power of two).
        U256 lane_delta;
        {
            U256 acc, mul = ks.inner_stride;
            uidx L = lane_len;
            while (L) {
                if (L & 1) acc = acc + mul;
                mul = mul + mul;
                L >>= 1;
            }
            lane_delta = acc;
        }
        PointA D;
        ec_mul_gen(D, lane_delta);
        lc.ec_mul_gen++;

        // Lane 0 anchor; every other lane is one mixed Jacobian add from it.
        U256 k0 = ks.scalar_at(backward ? (first + lane_len - 1) : first);
        PointA P0;
        ec_mul_gen(P0, k0);
        lc.ec_mul_gen++;

        // Chain the anchors in Jacobian and normalize them with ONE inversion.
        // Doing this with affine adds instead would cost B-1 inversions; with a
        // small --segment-size (so lane_len is 1) that dominates everything.
        lanes[0] = P0;
        if (B > 1) {
            PointJ acc;
            acc.X = P0.x; acc.Y = P0.y; acc.Z = Fe(1);
            for (int j = 1; j < B; j++) {
                point_j_add_affine(acc, acc, D);
                sc.jac[j] = acc;
                sc.zs[j] = acc.Z;
            }
            sc.zs[0] = Fe(1);
            batch_inverse(sc.zinv.data(), sc.zs.data(), (size_t)B);
            for (int j = 1; j < B; j++) {
                Fe zi = sc.zinv[j], zi2, zi3;
                fe_sqr(zi2, zi);
                fe_mul(zi3, zi2, zi);
                fe_mul(lanes[j].x, sc.jac[j].X, zi2);
                fe_mul(lanes[j].y, sc.jac[j].Y, zi3);
                lanes[j].inf = false;
            }
        }

        const int L = mh_->lanes;
        uint8_t* pubbuf = sc.pub.data();
        uint8_t* hbuf = sc.h160.data();
        // Publish counters every FLUSH_STEPS steps. A segment can be millions of
        // candidates, so flushing only at segment boundaries made --status report
        // 0 and then a catch-up spike. One atomic add per ~500k candidates is
        // free; leaving the progress line wrong is not.
        const uidx FLUSH_STEPS = 1024;
        uidx next_flush = FLUSH_STEPS;
        for (uidx t = 0; t < lane_len; t++) {
            if (stop_.load(std::memory_order_relaxed)) { completed = false; break; }
            if (t == next_flush) { lc.flush(counters_); next_flush += FLUSH_STEPS; }

            // 1. serialize every lane's point to a compressed pubkey
            for (int j = 0; j < B; j++) {
                const PointA& P = lanes[j];
                uint8_t* p = pubbuf + (size_t)j * 33;
                p[0] = P.y.is_odd() ? 0x03 : 0x02;
                fe_to_be32(P.x, p + 1);
            }
            // 2. SHA-256 + RIPEMD-160, `L` buffers at a time
            int j = 0;
            for (; j + L <= B; j += L)
                mh_->hash160(pubbuf + (size_t)j * 33, hbuf + (size_t)j * 20);
            for (; j < B; j++)
                hash160_pubkey33(pubbuf + (size_t)j * 33, hbuf + (size_t)j * 20);
            // 3. compare against the target set
            for (j = 0; j < B; j++) {
                const uint8_t* h = hbuf + (size_t)j * 20;
                if (__builtin_expect(targets_.maybe_contains(h), 0)) {
                    if (targets_.contains(h)) {
                        uidx local = backward ? (lane_len - 1 - t) : t;
                        uidx idx = first + (uidx)j * lane_len + local;
                        report_hit(ks.scalar_at(idx));
                    }
                }
            }
            lc.ec_points += (uint64_t)B;
            lc.sha256_ops += (uint64_t)B;
            lc.hash160_ops += (uint64_t)B;
            lc.candidates += (uint64_t)B;
            if (t + 1 < lane_len)
                batch_step_affine(lanes.data(), B, S, sc.prefix.data());
        }
    }

    // Tail: indices not covered by the lane grid, computed directly.
    for (uidx t = 0; t < tail; t++) {
        uidx idx = first + lane_len * (uidx)B + t;
        U256 k = ks.scalar_at(idx);
        PointA P;
        ec_mul_gen(P, k);
        uint8_t pub[33], sh[32], h160[20];
        pub[0] = P.y.is_odd() ? 0x03 : 0x02;
        fe_to_be32(P.x, pub + 1);
        sha256_33(pub, sh);
        ripemd160_32(sh, h160);
        if (targets_.maybe_contains(h160) && targets_.contains(h160)) report_hit(k);
        lc.ec_points++; lc.sha256_ops++; lc.hash160_ops++; lc.candidates++; lc.ec_mul_gen++;
    }
    return completed;
}

void Engine::report_hit(const U256& k) {
    // Never trust the hot loop. Recompute through the independent path with no
    // target supplied, then test the RECOMPUTED hash160 for membership - so the
    // acceptance decision never reuses anything the hot loop produced.
    VerifyResult vr = verify_candidate(k, nullptr);
    uint8_t h[20];
    bool in_set = vr.ok && from_hex(vr.hash160_hex, h, 20) && targets_.contains(h);

    std::lock_guard<std::mutex> lk(hits_mu_);
    if (in_set) {
        hits_.push_back({k, vr});
        found_.store(true, std::memory_order_relaxed);
        stop_.store(true, std::memory_order_relaxed);
        std::string err;
        // Persist before writing to a terminal/pipe that may already be closed.
        bool persisted = output_.append(vr, fingerprint_, err);
        printf("\n*** FOUND (confirmed by the independent verifier) ***\n"
               "  private : %s\n  wif     : %s\n  pubkey  : %s\n"
               "  hash160 : %s\n  address : %s\n",
               vr.priv_hex.c_str(),vr.wif.c_str(),vr.pub_compressed_hex.c_str(),
               vr.hash160_hex.c_str(),vr.address.c_str());
        fflush(stdout);
        if (!persisted) throw std::runtime_error(err);
    } else {
        fprintf(stderr, "[warn] hot-loop candidate %s REJECTED by verifier: %s\n",
                k.hex().c_str(), vr.ok ? "hash160 not in target set" : vr.error.c_str());
    }
}

void Engine::worker(int tid, int cpu) {
    if (cfg_.affinity && cpu >= 0 && !pin_thread_to_cpu(cpu))
        fprintf(stderr,"[warn] cannot pin worker %d to CPU %d\n",tid,cpu);
    LocalCounters lc;
    Scratch sc;
    sc.resize(batch_);

    KeySpace ks = ks_;
    uint64_t cur_pattern = UINT64_MAX;
    WorkUnit u;
    uint64_t flush = 0;

    while (!stop_.load(std::memory_order_relaxed) && sched_.claim(u)) {
        uint64_t pat = patterns_.ordinal_at(u.pattern_ordinal);
        if (pat != cur_pattern) {
            uint8_t m[32], v[32];
            patterns_.materialize(pat, m, v);
            memcpy(ks.mask, m, 32);
            memcpy(ks.value, v, 32);
            std::string e;
            if (!ks.finalize(e)) break;
            cur_pattern = pat;
        }
        // A segment interrupted half-way is deliberately left incomplete: the
        // watermark then stalls on it and a resume redoes it.  Redoing work is
        // safe; skipping it would silently drop candidates.
        if (!process(u, ks, lc, sc)) break;
        sched_.complete(u.ticket);
        lc.segments++;
        lc.flush(counters_);
        (void)flush;

        if (cfg_.limit_keys) {
            uint64_t done = counters_.candidates.load(std::memory_order_relaxed) + lc.candidates;
            if ((uidx)done >= cfg_.limit_keys) { stop_.store(true, std::memory_order_relaxed); break; }
        }
    }
    lc.flush(counters_);
    (void)tid;
}

BenchReport Engine::run() {
    std::string output_error;
    if (!output_.open(cfg_.output_path, output_error)) throw std::runtime_error(output_error);
    start_ = Clock::now();
    running_ = true;
    std::vector<int> cpus = affinity_plan(threads_, cfg_.avoid_smt);
    std::vector<std::thread> th;
    std::thread status, cp;
    std::exception_ptr failure;
    std::mutex failure_mutex;
    struct JoinWorkers {
        std::atomic<bool>& stop;
        std::vector<std::thread>& workers;
        std::thread& status;
        std::thread& checkpoint;
        ~JoinWorkers() {
            stop.store(true,std::memory_order_relaxed);
            for (auto& t:workers) if (t.joinable()) t.join();
            if (status.joinable()) status.join();
            if (checkpoint.joinable()) checkpoint.join();
        }
    } join{stop_,th,status,cp};
    for (int i = 0; i < threads_; i++) {
        int cpu = (i < (int)cpus.size()) ? cpus[i] : -1;
        th.emplace_back([this, i, cpu, &failure, &failure_mutex] {
            try { worker(i,cpu); }
            catch (...) {
                std::lock_guard<std::mutex> lock(failure_mutex);
                if (!failure) failure=std::current_exception();
                request_stop();
            }
        });
    }

    // Periodic progress line (keyhunt's -s equivalent).
    if (cfg_.status_interval_s > 0) {
        status = std::thread([this] {
            const double total = space_size();
            uint64_t last = 0;
            double last_t = 0;
            // A terminal gets one line rewritten in place; a pipe or file gets one
            // line per sample so the log stays greppable.
            const bool tty = isatty(fileno(stdout)) != 0;
            int  line_w = 0;
            bool printed = false;
            while (!stop_.load(std::memory_order_relaxed)) {
                for (int i = 0; i < cfg_.status_interval_s * 10; i++) {
                    if (stop_.load(std::memory_order_relaxed)) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (stop_.load(std::memory_order_relaxed)) break;
                double t = secs_since(start_);
                uint64_t done = counters_.candidates.load(std::memory_order_relaxed);
                double inst = (t > last_t) ? (done - last) / (t - last_t) : 0;
                double cumulative = (double)resumed_candidates_ + (double)done;
                double total_time = resumed_seconds_ + t;
                double avg  = (total_time > 0) ? cumulative / total_time : 0;
                last = done; last_t = t;

                // ETA off the average, not the instantaneous sample. Over a run
                // measured in days the average is both steadier and the better
                // predictor: the instantaneous figure swings with thermal state
                // and with counter-flush quantisation.
                char eta[64] = "--";
                if (avg > 0 && total > 0) {
                    double rem = (total - cumulative) / avg;
                    if (rem < 0) rem = 0;
                    if (rem < 3600)          snprintf(eta, sizeof(eta), "%.0fm", rem / 60);
                    else if (rem < 86400)    snprintf(eta, sizeof(eta), "%.1fh", rem / 3600);
                    else if (rem < 86400*365.0) snprintf(eta, sizeof(eta), "%.1fd", rem / 86400);
                    else                     snprintf(eta, sizeof(eta), "%.1fy", rem / (86400*365.0));
                }

                auto note = candidate_progress_note(done, counters_.outside_range_segments.load(std::memory_order_relaxed));
                char line[512];
                int n = snprintf(line, sizeof(line),
                        "[t+%02d:%02d:%02d] %s cand/s (avg %s) | done %s | %.6f%% | ETA %s%s%s",
                        (int)t / 3600, ((int)t / 60) % 60, (int)t % 60,
                        human_rate(inst).c_str(),
                        human_rate(avg).c_str(),
                        human_rate(cumulative).c_str(),
                        total > 0 ? std::min(100.0, 100.0 * cumulative / total) : 0.0,
                        eta, note.empty() ? "" : " | ", note.c_str());
                if (n < 0) n = 0;
                if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
                if (tty) {
                    // Pad to the widest line emitted so far, so a shorter line can
                    // never leave stale characters from the previous one behind.
                    if (n > line_w) line_w = n;
                    fprintf(stdout, "\r%-*s", line_w, line);
                    printed = true;
                } else {
                    fprintf(stdout, "%s\n", line);
                }
                fflush(stdout);
            }
            // Close the in-place line so the final report starts on a fresh row.
            if (tty && printed) { fputc('\n', stdout); fflush(stdout); }
        });
    }

    // Checkpoint / progress thread.
    if (!cfg_.checkpoint_path.empty()) {
        const int interval = cfg_.checkpoint_interval_s > 0 ? cfg_.checkpoint_interval_s : 30;
        cp = std::thread([this, interval] {
            while (!stop_.load(std::memory_order_relaxed)) {
                // Wake often enough to exit promptly, but only checkpoint on the
                // configured interval. A zero/negative interval would otherwise
                // spin writing the file continuously.
                for (int i = 0; i < interval * 10; i++) {
                    if (stop_.load(std::memory_order_relaxed)) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                std::string e;
                if (!write_checkpoint(e)) {
                    checkpoint_failed_.store(true);
                    fprintf(stderr, "[warn] checkpoint write failed: %s\n", e.c_str());
                }
            }
        });
    }

    for (auto& t : th) t.join();
    stop_.store(true, std::memory_order_relaxed);
    if (status.joinable()) status.join();
    if (cp.joinable()) cp.join();
    if (!cfg_.checkpoint_path.empty()) {
        std::string e;
        if (!write_checkpoint(e)) {
            checkpoint_failed_.store(true);
            fprintf(stderr, "error: final checkpoint write failed: %s\n", e.c_str());
        }
    }

    if (failure) std::rethrow_exception(failure);
    BenchReport r;
    r.seconds = secs_since(start_);
    r.threads = threads_;
    r.candidates  = counters_.candidates.load();
    r.ec_points   = counters_.ec_points.load();
    r.sha256_ops  = counters_.sha256_ops.load();
    r.hash160_ops = counters_.hash160_ops.load();
    r.ec_mul_gen  = counters_.ec_mul_gen.load();
    r.segments    = counters_.segments.load();
    r.outside_range_segments = counters_.outside_range_segments.load();
    return r;
}

bool Engine::write_checkpoint(std::string& err) {
    if (cfg_.checkpoint_path.empty()) { err="no checkpoint path configured"; return false; }
    Checkpoint cp;
    cp.puzzle = cfg_.puzzle;
    cp.range_lo = ks_.range_lo.hex();
    cp.range_hi = ks_.range_hi.hex();
    // Record the actual target(s); with --file there may be many, and writing
    // the unset single-target buffer would show a misleading row of zeros.
    if (cfg_.has_target) {
        cp.target_hash160 = to_hex(cfg_.target, 20);
        if (!cfg_.extra_targets.empty())
            cp.target_hash160 += " +" + std::to_string(cfg_.extra_targets.size());
    } else if (!cfg_.extra_targets.empty()) {
        cp.target_hash160 = to_hex(cfg_.extra_targets[0].data(), 20);
        if (cfg_.extra_targets.size() > 1)
            cp.target_hash160 += " +" + std::to_string(cfg_.extra_targets.size() - 1);
    }
    cp.global_order = order_name(cfg_.global_order);
    cp.local_direction = direction_name(cfg_.local_dir);
    cp.seed = cfg_.seed;
    cp.fixed_enabled = cfg_.fixed.enabled;
    cp.fixed_mask = to_hex(ks_.mask, 32);
    cp.fixed_value = to_hex(ks_.value, 32);
    cp.fixed_rotation = order_name(cfg_.fixed_rotation);
    cp.fixed_positions = patterns_.positions();
    cp.round_robin = cfg_.round_robin;
    cp.slice_keys = uidx_to_dec(sched_.effective_slice_segs() * seg_size_);
    cp.segment_size = uidx_to_dec(seg_size_);
    cp.config_hash = fingerprint_;

    Scheduler::Progress p = sched_.snapshot();
    cp.watermark = p.watermark;
    cp.done_above = p.done_above;

    // Derived, human-readable position of the frontier.
    uint64_t slot = 0;
    uidx seg_ord = 0;
    if (segs_per_pattern_) {
        if (cfg_.round_robin) {
            uidx ss = sched_.effective_slice_segs();
            uint64_t per_round = patterns_.count() * (uint64_t)ss;
            uint64_t round = per_round ? p.watermark / per_round : 0;
            uint64_t rem = per_round ? p.watermark % per_round : 0;
            slot = ss ? rem / (uint64_t)ss : 0;
            seg_ord = (uidx)round * ss + (uidx)(ss ? rem % (uint64_t)ss : 0);
        } else {
            slot = (uint64_t)((uidx)p.watermark / segs_per_pattern_);
            seg_ord = (uidx)p.watermark % segs_per_pattern_;
        }
    }
    cp.current_pattern = slot < patterns_.count() ? patterns_.ordinal_at(slot) : 0;
    cp.current_segment = uidx_to_dec(seg_ord);
    cp.local_offset = uidx_to_dec(seg_ord * seg_size_);
    cp.total_candidates = uidx_to_dec((uidx)resumed_candidates_ + counters_.candidates.load());
    cp.elapsed_seconds = resumed_seconds_ + (running_ ? secs_since(start_) : 0.0);
    return cp.save(cfg_.checkpoint_path, err);
}

void Engine::enumerate_all(const std::function<void(const U256&)>& cb) {
    KeySpace ks = ks_;
    WorkUnit u;
    while (sched_.claim(u)) {
        uint64_t pat = patterns_.ordinal_at(u.pattern_ordinal);
        uint8_t m[32], v[32];
        patterns_.materialize(pat, m, v);
        memcpy(ks.mask, m, 32);
        memcpy(ks.value, v, 32);
        std::string e;
        if (!ks.finalize(e)) break;

        uidx count = u.index_end - u.index_begin;
        if (cfg_.local_dir == Direction::Backward) {
            for (uidx t = count; t-- > 0;) {
                U256 k = ks.scalar_at(u.index_begin + t);
                if (ks.in_range(k)) cb(k);
            }
        } else {
            for (uidx t = 0; t < count; t++) {
                U256 k = ks.scalar_at(u.index_begin + t);
                if (ks.in_range(k)) cb(k);
            }
        }
        sched_.complete(u.ticket);
    }
}

} // namespace ex369
