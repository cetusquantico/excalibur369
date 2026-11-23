// EXCALIBUR369 - throughput counters.
//
// The headline metric is FULL VALID CANDIDATES PER SECOND: a candidate is only
// counted once it has been through EC point -> compressed pubkey -> SHA-256 ->
// RIPEMD-160 -> target comparison.  Threads accumulate locally and flush rarely,
// so counting never touches the hot path's memory traffic.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace ex369 {

struct Counters {
    std::atomic<uint64_t> ec_points{0};
    std::atomic<uint64_t> sha256_ops{0};
    std::atomic<uint64_t> hash160_ops{0};
    std::atomic<uint64_t> candidates{0};   // full pipeline + comparison
    std::atomic<uint64_t> segments{0};
    std::atomic<uint64_t> outside_range_segments{0}; // discarded without candidate checks
    std::atomic<uint64_t> ec_mul_gen{0};   // block-start scalar multiplications
};

struct LocalCounters {
    uint64_t ec_points = 0, sha256_ops = 0, hash160_ops = 0;
    uint64_t candidates = 0, segments = 0, ec_mul_gen = 0;
    uint64_t outside_range_segments = 0;

    void flush(Counters& g) {
        if (ec_points)   g.ec_points.fetch_add(ec_points, std::memory_order_relaxed);
        if (sha256_ops)  g.sha256_ops.fetch_add(sha256_ops, std::memory_order_relaxed);
        if (hash160_ops) g.hash160_ops.fetch_add(hash160_ops, std::memory_order_relaxed);
        if (candidates)  g.candidates.fetch_add(candidates, std::memory_order_relaxed);
        if (segments)    g.segments.fetch_add(segments, std::memory_order_relaxed);
        if (outside_range_segments) g.outside_range_segments.fetch_add(outside_range_segments, std::memory_order_relaxed);
        if (ec_mul_gen)  g.ec_mul_gen.fetch_add(ec_mul_gen, std::memory_order_relaxed);
        ec_points = sha256_ops = hash160_ops = candidates = segments = ec_mul_gen = 0;
        outside_range_segments = 0;
    }
};

using Clock = std::chrono::steady_clock;

inline double secs_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

std::string human_rate(double v);
std::string candidate_progress_note(uint64_t candidates, uint64_t outside_range_segments);

struct BenchReport {
    double   seconds = 0;
    int      threads = 1;
    uint64_t candidates = 0, ec_points = 0, sha256_ops = 0, hash160_ops = 0;
    uint64_t ec_mul_gen = 0, segments = 0;
    uint64_t outside_range_segments = 0;
    double candidates_per_sec() const { return seconds > 0 ? candidates / seconds : 0; }
    double per_core_per_sec() const   { return threads > 0 ? candidates_per_sec() / threads : 0; }
    std::string format(const std::string& label) const;
};

} // namespace ex369
