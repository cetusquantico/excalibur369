// EXCALIBUR369 - segment scheduler.
//
// All work is addressed by a single monotonic uint64 TICKET.  A ticket maps
// deterministically to (pattern slot, segment ordinal), and both of those map
// through bijections (identity / reversal / keyed PRP) to the actual pattern and
// segment.  Consequences:
//
//   * claiming work is one atomic fetch_add - no mutex on the hot path;
//   * two threads can never receive the same segment (no overlap);
//   * the whole distribution is reproducible from (seed, config), so a future
//     multi-machine mode only needs to hand out disjoint ticket intervals;
//   * a checkpoint is a watermark plus the few out-of-order completions.
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <vector>
#include <string>
#include "../config.h"
#include "../traversal/prp.h"

namespace ex369 {

struct WorkUnit {
    uint64_t ticket = 0;
    uint64_t pattern_ordinal = 0;
    uidx     segment_index = 0;    // segment index inside the pattern's space
    uidx     index_begin = 0;      // enumeration index range covered
    uidx     index_end = 0;
};

class Scheduler {
public:
    // segs_per_pattern: number of segments covering one pattern's index space.
    bool init(uint64_t npatterns, uidx segs_per_pattern, uidx segment_size,
              uidx total_indices, bool round_robin, uidx slice_segs,
              Order segment_order, uint64_t seed, std::string& err);

    // Returns false once every ticket has been handed out.
    bool claim(WorkUnit& out);
    void complete(uint64_t ticket);

    uint64_t total_tickets() const { return total_tickets_; }
    uidx     effective_slice_segs() const { return slice_segs_; }

    // --- checkpoint support (called off the hot path) ---
    struct Progress {
        uint64_t watermark = 0;              // every ticket < watermark is done
        std::vector<uint64_t> done_above;    // completed out-of-order
    };
    Progress snapshot() const;
    bool restore(const Progress& p, std::string& err);

private:
    uint64_t npatterns_ = 1;
    uidx     segs_per_pattern_ = 1;
    uidx     segment_size_ = 1;
    uidx     total_indices_ = 1;
    bool     round_robin_ = false;
    uidx     slice_segs_ = 1;
    uidx     rounds_ = 1;                    // ceil(segs_per_pattern / slice_segs)
    Order    seg_order_ = Order::Sequential;
    Prp      seg_prp_;
    uint64_t total_tickets_ = 0;

    std::atomic<uint64_t> next_{0};

    mutable std::mutex mu_;
    uint64_t watermark_ = 0;
    std::set<uint64_t> done_above_;
    std::vector<uint64_t> resumed_done_;      // sorted, read-only after restore

    bool already_done(uint64_t ticket) const;
    bool decode(uint64_t ticket, uint64_t& pattern_slot, uidx& seg_ordinal) const;
    uidx segment_at(uidx ordinal) const;
};

} // namespace ex369
