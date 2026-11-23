// EXCALIBUR369 - search engine.
#pragma once
#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include "config.h"
#include "ec/group.h"
#include "fixed/mask.h"
#include "fixed/pattern.h"
#include "scheduler/scheduler.h"
#include "benchmark/stats.h"
#include "verify/verify.h"
#include "hash/hash_multi.h"
#include "io/run_files.h"

namespace ex369 {

// Target HASH160 set. A 64 Kbit filter keyed on the first 8 bytes keeps the hot
// path to one load + test in the (overwhelmingly common) miss case.
class TargetSet {
public:
    void add(const uint8_t h[20]);
    void build();
    size_t size() const { return items_.size(); }
    inline bool maybe_contains(const uint8_t h[20]) const {
        uint64_t k;
        memcpy(&k, h, 8);
        return (bloom_[(k >> 3) & (BLOOM_WORDS - 1)] >> (k & 63)) & 1ULL;
    }
    bool contains(const uint8_t h[20]) const;
private:
    static constexpr size_t BLOOM_WORDS = 1024;
    uint64_t bloom_[BLOOM_WORDS] = {0};
    std::vector<std::array<uint8_t, 20>> items_;
};

struct Hit {
    U256 priv;
    VerifyResult verified;
};

struct Scratch;

class Engine {
public:
    explicit Engine(const SearchConfig& cfg) : cfg_(cfg) {}
    ~Engine();

    bool prepare(std::string& err);
    BenchReport run();
    bool write_checkpoint(std::string& err);

    void request_stop() { stop_.store(true, std::memory_order_relaxed); }
    bool found() const { return found_.load(std::memory_order_relaxed); }
    const std::vector<Hit>& hits() const { return hits_; }

    const SearchConfig& config() const { return cfg_; }
    const KeySpace& keyspace() const { return ks_; }
    const PatternSpace& patterns() const { return patterns_; }
    uidx segments_per_pattern() const { return segs_per_pattern_; }
    uidx effective_segment_size() const { return seg_size_; }
    Scheduler& scheduler() { return sched_; }
    std::string plan_description() const;
    // Total candidates the current configuration will ever test.
    double space_size() const;
    const char* hash_backend() const { return mh_ ? mh_->name : "?"; }
    bool checkpoint_failed() const { return checkpoint_failed_.load(); }

    // Test aid: single-threaded walk of the whole space in visit order.
    void enumerate_all(const std::function<void(const U256&)>& cb);

private:
    void worker(int tid, int cpu);
    // Returns false if the segment was cut short by a stop request; such a
    // segment must NOT be marked complete or a resume would skip its keys.
    bool process(const WorkUnit& u, const KeySpace& ks, LocalCounters& lc, Scratch& sc);
    void report_hit(const U256& k);

    SearchConfig  cfg_;
    KeySpace      ks_;
    PatternSpace  patterns_;
    Scheduler     sched_;
    Counters      counters_;
    TargetSet     targets_;

    const MultiHash* mh_ = nullptr;

    uidx  seg_size_ = 1;
    uidx  segs_per_pattern_ = 1;
    int   batch_ = 256;
    int   threads_ = 1;

    std::atomic<bool> stop_{false};
    std::atomic<bool> found_{false};
    std::mutex        hits_mu_;
    std::vector<Hit>  hits_;
    ResultJournal    output_;

    Clock::time_point start_;
    std::string       fingerprint_;
    uint64_t          resumed_candidates_ = 0;
    double            resumed_seconds_ = 0;
    bool              running_ = false;
    std::atomic<bool> checkpoint_failed_{false};
};

} // namespace ex369
