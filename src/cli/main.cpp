// EXCALIBUR369 - CPU-first key hunter for PUBLIC Bitcoin puzzle ranges.
#include <cstdio>
#include <cstring>
#include <csignal>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <array>
#include <climits>
#include <stdexcept>
#include "../config.h"
#include "../engine.h"
#include "../cpu/cpu.h"
#include "../verify/verify.h"
#include "../diagnostics/self_test.h"
#include "../io/run_files.h"

using namespace ex369;

static std::atomic<Engine*> g_engine{nullptr};
static_assert(std::atomic<Engine*>::is_always_lock_free && std::atomic<bool>::is_always_lock_free,
              "Signal handling requires lock-free atomics");
static void on_sigint(int) {
    if (auto* engine = g_engine.load(std::memory_order_relaxed)) engine->request_stop();
}

static const char* BANNER =
"  EXCALIBUR369  -  CPU-first key hunter\n"
"  For PUBLIC puzzle ranges only.\n";

static void usage() {
    printf("%s\n", BANNER);
    printf(
"usage: kh71 [options]\n"
"\n"
"target / range\n"
"  --puzzle N               search Bitcoin puzzle N (range [2^(N-1), 2^N-1])\n"
"  --range LO:HI            explicit hex range instead of --puzzle\n"
"  --address ADDR           target P2PKH address (Base58Check)\n"
"  --hash160 HEX            target HASH160 directly (40 hex chars)\n"
"  --file PATH, -f PATH     file of targets, one address or HASH160 per line\n"
"\n"
"traversal\n"
"  --mode MODE              sequential | reverse | random     (segment ordering)\n"
"  --direction DIR          forward | reverse  (inside a segment; defaults to\n"
"                           reverse when --mode reverse is used alone)\n"
"  --seed N                 seed for random-no-repeat orderings\n"
"  --segment-size N         keys per segment (rounded down to a power of two)\n"
"\n"
"fixed bytes\n"
"  --fixed on|off           enable the fixed-byte engine\n"
"  --fixed-count N          number of fixed bytes (with --fixed-positions auto)\n"
"  --fixed-positions SPEC   'auto' or a comma list, e.g. 1,2,3\n"
"  --fixed-values HEX,...   manual values, e.g. AA,BB,CC (omit to rotate)\n"
"  --fixed-rotation MODE    sequential | reverse | random-no-repeat\n"
"  --fixed-index-base B     active | absolute  (how positions are numbered)\n"
"\n"
"round-robin\n"
"  --round-robin on|off     interleave fixed-byte patterns\n"
"  --slice-keys N           keys processed per pattern before switching\n"
"\n"
"execution\n"
"  --threads N              worker threads (default: physical cores)\n"
"  --affinity on|off        pin threads to CPUs\n"
"  --smt on|off             allow SMT siblings (default off)\n"
"  --batch N                EC lanes per batch inversion (default 2048)\n"
"  --simd BACKEND           scalar | sse | avx2 (default: best available)\n"
"\n"
"checkpoint\n"
"  --checkpoint PATH        checkpoint file\n"
"  --checkpoint-interval S  seconds between checkpoints (default 30)\n"
"  --resume                 resume from the checkpoint file\n"
"  --output PATH            append confirmed results to private JSONL (0600)\n"
"\n"
"misc\n"
"  --status S               print a progress line every S seconds\n"
"  --limit N                stop after N candidates (benchmark aid)\n"
"  --dry-run                print the plan and exit\n"
"  --cpu-info               print CPU capabilities and exit\n"
"  --build-info             print compiler, field and compiled backends\n"
"  --self-test              check known vectors and usable backends, then exit\n"
"  --version                print the release version and exit\n"
"  --verify KEYHEX          run the independent verifier on one key and exit\n"
"  -h, --help               this help\n"
"\n"
"examples\n"
"  kh71 --puzzle 71 --address 1PWo3JeB9jrGwfHDNpdGeLc6tTzsfrHwJx --mode sequential\n"
"  kh71 --puzzle 71 --address 1PWo... --mode reverse\n"
"  kh71 --puzzle 71 --address 1PWo... --mode random --seed 12345\n"
"  kh71 --puzzle 71 --address 1PWo... --fixed on --fixed-count 3 \\\n"
"       --fixed-positions auto --fixed-rotation random-no-repeat\n"
"  kh71 --puzzle 71 --address 1PWo... --fixed on --fixed-values AA,BB,CC \\\n"
"       --fixed-positions 1,2,3 --round-robin on --slice-keys 1073741824\n");
}

// Load a target list: one entry per line, either a Base58Check P2PKH address or
// a 40-hex HASH160. Blank lines and '#' comments are ignored. Anything else is
// an error, rather than being skipped silently - a typo in a target file would
// otherwise mean searching for the wrong thing for days.
static bool load_targets(const std::string& path,
                         std::vector<std::array<uint8_t, 20>>& out,
                         std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open " + path; return false; }
    std::string line;
    const size_t before = out.size();
    int lineno = 0;
    while (std::getline(f, line)) {
        lineno++;
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r\n");
        line = line.substr(a, b - a + 1);
        if (line.empty() || line[0] == '#') continue;
        if (line.size() > 64) { err = path + ":" + std::to_string(lineno) + ": target line too long"; return false; }

        std::array<uint8_t, 20> h{};
        if (line.size() == 40 && from_hex(line, h.data(), 20)) {
            out.push_back(h);
        } else if (address_to_hash160(line, h.data())) {
            out.push_back(h);
        } else {
            err = path + ":" + std::to_string(lineno) +
                  ": not a mainnet P2PKH address or 40-hex HASH160: " + line;
            return false;
        }
    }
    if (f.bad()) { err = "read error in " + path; return false; }
    if (out.size() == before) { err = path + ": no targets found"; return false; }
    return true;
}

static bool on_off(const std::string& s, bool& out) {
    if (s == "on" || s == "true" || s == "1" || s == "yes") { out = true; return true; }
    if (s == "off" || s == "false" || s == "0" || s == "no") { out = false; return true; }
    return false;
}

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> v;
    if (s.empty() || s.back() == sep) return v;
    std::string cur;
    std::istringstream is(s);
    while (std::getline(is, cur, sep)) v.push_back(cur);
    return v;
}

int main(int argc, char** argv) {
    SearchConfig cfg;
    bool dry_run = false, want_cpu = false;
    bool want_build = false, want_test = false, verify_given = false;
    bool direction_given = false;
    std::string verify_key;
    std::vector<std::string> target_paths;

    auto need = [&](int& i) -> std::string {
        if (i + 1 >= argc) { fprintf(stderr, "error: %s needs a value\n", argv[i]); exit(2); }
        return argv[++i];
    };

    auto integer = [&](const std::string& value, int lo, int hi, const char* flag) -> int {
        uidx n;
        if (!uidx_from_dec(value, n) || n < (uidx)lo || n > (uidx)hi) {
            fprintf(stderr, "error: %s requires an integer in %d..%d\n", flag, lo, hi);
            exit(2);
        }
        return (int)n;
    };

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto fail = [&](const std::string& m) { fprintf(stderr, "error: %s\n", m.c_str()); exit(2); };

        if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (a == "--version") { printf("EXCALIBUR369 %s\n", EX369_VERSION); return 0; }
        else if (a == "--cpu-info") want_cpu = true;
        else if (a == "--build-info") want_build = true;
        else if (a == "--self-test") want_test = true;
        else if (a == "--dry-run") dry_run = true;
        else if (a == "--verify") { verify_key = need(i); verify_given = true; }
        else if (a == "--puzzle") cfg.puzzle = integer(need(i), 1, 160, "--puzzle");
        else if (a == "--range") {
            auto p = split(need(i), ':');
            if (p.size() != 2) fail("--range wants LO:HI in hex");
            if (!U256::from_hex(p[0], cfg.range_lo) || !U256::from_hex(p[1], cfg.range_hi))
                fail("bad hex in --range");
            cfg.puzzle = 0;
        }
        else if (a == "--address") {
            cfg.target_address = need(i);
            if (!address_to_hash160(cfg.target_address, cfg.target))
                fail("not a valid mainnet P2PKH address: " + cfg.target_address);
            cfg.has_target = true;
        }
        else if (a == "--file" || a == "-f") {
            cfg.target_file = need(i);
            target_paths.push_back(cfg.target_file);
            std::string e;
            if (!load_targets(cfg.target_file, cfg.extra_targets, e)) fail(e);
        }
        else if (a == "--status") cfg.status_interval_s = integer(need(i), 0, 86400, "--status");
        else if (a == "--hash160") {
            if (!from_hex(need(i), cfg.target, 20)) fail("--hash160 wants 40 hex chars");
            cfg.has_target = true;
        }
        else if (a == "--mode") { if (!parse_order(need(i), cfg.global_order)) fail("bad --mode"); }
        else if (a == "--direction") {
            std::string d = need(i);
            if (d == "forward") cfg.local_dir = Direction::Forward;
            else if (d == "reverse" || d == "backward") cfg.local_dir = Direction::Backward;
            else fail("bad --direction");
            direction_given = true;
        }
        else if (a == "--seed") { uidx n; if (!uidx_from_dec(need(i), n) || n > UINT64_MAX) fail("bad --seed"); cfg.seed = (uint64_t)n; }
        else if (a == "--segment-size") { if (!uidx_from_dec(need(i), cfg.segment_size) || cfg.segment_size == 0) fail("bad --segment-size"); }
        else if (a == "--fixed") { if (!on_off(need(i), cfg.fixed.enabled)) fail("--fixed wants on|off"); }
        else if (a == "--fixed-count") cfg.fixed.count = integer(need(i), 1, 31, "--fixed-count");
        else if (a == "--fixed-positions") {
            std::string v = need(i);
            if (v == "auto") cfg.fixed.positions_auto = true;
            else {
                cfg.fixed.positions_auto = false;
                cfg.fixed.positions.clear();
                if (split(v, ',').empty()) fail("empty --fixed-positions");
                for (auto& t : split(v, ',')) cfg.fixed.positions.push_back(integer(t, 0, 31, "--fixed-positions"));
            }
        }
        else if (a == "--fixed-values") {
            cfg.fixed.values_auto = false;
            cfg.fixed.values.clear();
            auto values = split(need(i), ',');
            if (values.empty()) fail("empty --fixed-values");
            for (auto& t : values) {
                uint8_t b;
                if (!from_hex(t.size() == 1 ? ("0" + t) : t, &b, 1)) fail("bad byte in --fixed-values: " + t);
                cfg.fixed.values.push_back(b);
            }
        }
        else if (a == "--fixed-rotation") { if (!parse_order(need(i), cfg.fixed_rotation)) fail("bad --fixed-rotation"); }
        else if (a == "--fixed-index-base") {
            std::string v = need(i);
            if (v == "absolute") cfg.fixed.absolute_positions = true;
            else if (v == "active") cfg.fixed.absolute_positions = false;
            else fail("--fixed-index-base wants active|absolute");
        }
        else if (a == "--round-robin") { if (!on_off(need(i), cfg.round_robin)) fail("--round-robin wants on|off"); }
        else if (a == "--slice-keys") { if (!uidx_from_dec(need(i), cfg.slice_keys) || cfg.slice_keys == 0) fail("bad --slice-keys"); }
        else if (a == "--threads") cfg.threads = integer(need(i), 1, 1024, "--threads");
        else if (a == "--affinity") { if (!on_off(need(i), cfg.affinity)) fail("--affinity wants on|off"); }
        else if (a == "--smt") { bool v; if (!on_off(need(i), v)) fail("--smt wants on|off"); cfg.avoid_smt = !v; }
        else if (a == "--batch") cfg.batch = integer(need(i), 1, 65536, "--batch");
        else if (a == "--simd") { cfg.simd = need(i); if (cfg.simd != "scalar" && cfg.simd != "sse" && cfg.simd != "avx2") fail("--simd must be scalar, sse or avx2"); }
        else if (a == "--checkpoint") cfg.checkpoint_path = need(i);
        else if (a == "--output") { cfg.output_path = need(i); if (cfg.output_path.empty()) fail("--output needs a nonempty path"); }
        else if (a == "--checkpoint-interval") cfg.checkpoint_interval_s = integer(need(i), 1, 86400, "--checkpoint-interval");
        else if (a == "--resume") cfg.resume = true;
        else if (a == "--limit") { if (!uidx_from_dec(need(i), cfg.limit_keys)) fail("bad --limit"); }
        else if (a == "--quiet") cfg.quiet = true;
        else fail("unknown option " + a);
    }

    // `--mode reverse` on its own means "search backwards", so it also flips the
    // in-segment walk to P -= G. Passing --direction explicitly overrides that,
    // which is what keeps GLOBAL ordering and LOCAL direction independent.
    if (cfg.global_order == Order::Reverse && !direction_given)
        cfg.local_dir = Direction::Backward;

    if (want_build || want_test) printf("%s\n", build_description().c_str());
    if (want_cpu || want_test) {
        printf("%s\n", cpu_info().summary().c_str());
        printf("  compiled backends: %s\n", compiled_backends().c_str());
        printf("  simd backend selected: %s\n", multihash_for(select_backend(cfg.simd.c_str())).name);
    }
    if (want_test) {
        std::string report;
        bool ok = self_test(report);
        printf("%s", report.c_str());
        return ok ? 0 : 1;
    }
    if (want_cpu || want_build) return 0;

    std::string err;
    if (!canonical_run_path(cfg.checkpoint_path, cfg.checkpoint_path, err) ||
        !canonical_run_path(cfg.output_path, cfg.output_path, err) ||
        !run_paths_distinct(cfg.checkpoint_path, cfg.output_path, "", err)) {
        fprintf(stderr, "error: %s\n", err.c_str()); return 1;
    }
    for (auto& path : target_paths) {
        if (!canonical_run_path(path, path, err) ||
            !run_paths_distinct(cfg.checkpoint_path, cfg.output_path, path, err)) {
            fprintf(stderr, "error: %s\n", err.c_str()); return 1;
        }
    }

    if (verify_given) {
        if (dry_run) { fprintf(stderr, "error: --verify and --dry-run are separate operations\n"); return 2; }
        U256 k;
        if (!U256::from_hex(verify_key, k)) { fprintf(stderr, "bad key hex\n"); return 2; }
        VerifyResult r = verify_candidate(k, cfg.has_target ? cfg.target : nullptr);
        if (!r.ok) { fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
        PathLock lock;
        ResultJournal output;
        bool persisted = lock.acquire(cfg.output_path, err) && output.open(cfg.output_path, err) &&
                         output.append(r, "", err);
        printf("private   : %s\n", r.priv_hex.c_str());
        printf("wif       : %s\n", r.wif.c_str());
        printf("pubkey    : %s\n", r.pub_compressed_hex.c_str());
        printf("hash160   : %s\n", r.hash160_hex.c_str());
        printf("address   : %s\n", r.address.c_str());
        if (cfg.has_target) printf("match     : %s\n", r.ok ? "YES" : ("no (" + r.error + ")").c_str());
        fflush(stdout);
        if (!persisted) {
            fprintf(stderr, "error: %s\n", err.c_str()); return 1;
        }
        return 0;
    }

    if (cfg.puzzle == 0 && cfg.range_hi.is_zero()) {
        fprintf(stderr, "error: need --puzzle N or --range LO:HI (see --help)\n");
        return 2;
    }
    if (!cfg.has_target && cfg.extra_targets.empty()) {
        fprintf(stderr, "error: need --address, --hash160 or --file\n");
        return 2;
    }

    if (!cfg.quiet) {
        printf("%s\n", BANNER);
        printf("%s\n", cpu_info().summary().c_str());
        printf("  simd backend   : %s\n\n", multihash_for(select_backend(cfg.simd.c_str())).name);
    }

    PathLock checkpoint_lock, output_lock;
    if (!dry_run && (!checkpoint_lock.acquire(cfg.checkpoint_path, err) ||
                     !output_lock.acquire(cfg.output_path, err))) {
        fprintf(stderr, "error: %s\n", err.c_str()); return 1;
    }
    Engine eng(cfg);
    if (!eng.prepare(err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (!cfg.quiet) {
        size_t ntargets = cfg.extra_targets.size() + (cfg.has_target ? 1 : 0);
        if (ntargets == 1) {
            const uint8_t* t = cfg.has_target ? cfg.target : cfg.extra_targets[0].data();
            printf("target hash160  : %s\n", to_hex(t, 20).c_str());
            if (!cfg.target_address.empty())
                printf("target address  : %s\n", cfg.target_address.c_str());
        } else {
            printf("targets         : %zu loaded from %s\n", ntargets, cfg.target_file.c_str());
        }
        printf("%s\n", eng.plan_description().c_str());
    }
    if (dry_run) return 0;

    g_engine = &eng;
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    BenchReport r;
    try { r = eng.run(); }
    catch (const std::exception& e) {
        g_engine = nullptr;
        fprintf(stderr, "error: execution failed: %s\n", e.what());
        return 1;
    }
    g_engine = nullptr;
    printf("\n%s", r.format("run").c_str());

    return eng.checkpoint_failed() ? 1 : 0;
}
