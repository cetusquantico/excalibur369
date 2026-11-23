#include "scheduler.h"
#include <algorithm>

namespace ex369 {

static bool mul_overflow_u64(uint64_t a, uint64_t b, uint64_t& r) {
    if (a != 0 && b > UINT64_MAX / a) return true;
    r = a * b;
    return false;
}

bool Scheduler::init(uint64_t npatterns, uidx segs_per_pattern, uidx segment_size,
                     uidx total_indices, bool round_robin, uidx slice_segs,
                     Order segment_order, uint64_t seed, std::string& err) {
    npatterns_ = npatterns ? npatterns : 1;
    segs_per_pattern_ = segs_per_pattern ? segs_per_pattern : 1;
    segment_size_ = segment_size;
    total_indices_ = total_indices;
    round_robin_ = round_robin;
    seg_order_ = segment_order;

    if (segs_per_pattern_ > (uidx)UINT64_MAX) {
        err = "too many segments per pattern; increase --segment-size";
        return false;
    }

    slice_segs_ = slice_segs ? slice_segs : 1;
    if (slice_segs_ > segs_per_pattern_) slice_segs_ = segs_per_pattern_;
    rounds_ = (segs_per_pattern_ + slice_segs_ - 1) / slice_segs_;

    uint64_t tickets;
    if (round_robin_) {
        uint64_t per_round;
        if (mul_overflow_u64(npatterns_, (uint64_t)slice_segs_, per_round) ||
            mul_overflow_u64(per_round, (uint64_t)rounds_, tickets)) {
            err = "ticket space overflows 64 bits; increase --segment-size or --slice-keys";
            return false;
        }
    } else {
        if (mul_overflow_u64(npatterns_, (uint64_t)segs_per_pattern_, tickets)) {
            err = "ticket space overflows 64 bits; increase --segment-size";
            return false;
        }
    }
    total_tickets_ = tickets;

    if (seg_order_ == Order::Random) seg_prp_.init(segs_per_pattern_, seed);
    next_.store(0, std::memory_order_relaxed);
    watermark_ = 0;
    done_above_.clear();
    resumed_done_.clear();
    return true;
}

uidx Scheduler::segment_at(uidx ordinal) const {
    switch (seg_order_) {
        case Order::Sequential: return ordinal;
        case Order::Reverse:    return segs_per_pattern_ - 1 - ordinal;
        default:                return seg_prp_.at(ordinal);
    }
}

bool Scheduler::decode(uint64_t ticket, uint64_t& pattern_slot, uidx& seg_ordinal) const {
    if (round_robin_) {
        uint64_t per_round = npatterns_ * (uint64_t)slice_segs_;
        uint64_t round = ticket / per_round;
        uint64_t rem   = ticket % per_round;
        pattern_slot = rem / (uint64_t)slice_segs_;
        uidx within  = (uidx)(rem % (uint64_t)slice_segs_);
        seg_ordinal  = (uidx)round * slice_segs_ + within;
    } else {
        pattern_slot = (uint64_t)((uidx)ticket / segs_per_pattern_);
        seg_ordinal  = (uidx)ticket % segs_per_pattern_;
    }
    // Round-robin can address past the end of a pattern's space on the last round.
    return seg_ordinal < segs_per_pattern_ && pattern_slot < npatterns_;
}

bool Scheduler::already_done(uint64_t ticket) const {
    if (resumed_done_.empty()) return false;
    return std::binary_search(resumed_done_.begin(), resumed_done_.end(), ticket);
}

bool Scheduler::claim(WorkUnit& out) {
    for (;;) {
        uint64_t t = next_.fetch_add(1, std::memory_order_relaxed);
        if (t >= total_tickets_) return false;

        uint64_t slot;
        uidx ord;
        if (!decode(t, slot, ord)) { complete(t); continue; }   // void ticket
        if (already_done(t))       { complete(t); continue; }   // finished before resume

        uidx seg = segment_at(ord);
        uidx begin = seg * segment_size_;
        if (begin >= total_indices_) { complete(t); continue; }
        uidx end = begin + segment_size_;
        if (end > total_indices_) end = total_indices_;

        out.ticket = t;
        out.pattern_ordinal = slot;      // caller maps slot -> pattern ordinal
        out.segment_index = seg;
        out.index_begin = begin;
        out.index_end = end;
        return true;
    }
}

void Scheduler::complete(uint64_t ticket) {
    std::lock_guard<std::mutex> lk(mu_);
    if (ticket < watermark_) return;
    // The usual in-order completion needs no temporary tree allocation.
    if (ticket == watermark_) watermark_++;
    else done_above_.insert(ticket);
    while (!done_above_.empty() && *done_above_.begin() == watermark_) {
        done_above_.erase(done_above_.begin());
        watermark_++;
    }
}

Scheduler::Progress Scheduler::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    Progress p;
    p.watermark = watermark_;
    p.done_above.assign(done_above_.begin(), done_above_.end());
    return p;
}

bool Scheduler::restore(const Progress& p, std::string& err) {
    std::lock_guard<std::mutex> lk(mu_);
    if (p.watermark > total_tickets_) { err = "checkpoint watermark exceeds ticket count"; return false; }
    std::set<uint64_t> unique;
    for (uint64_t t : p.done_above) {
        if (t <= p.watermark || t >= total_tickets_ || !unique.insert(t).second) {
            err = "checkpoint contains invalid or duplicate completed tickets";
            return false;
        }
    }
    watermark_ = p.watermark;
    done_above_ = std::move(unique);
    resumed_done_ = p.done_above;
    std::sort(resumed_done_.begin(), resumed_done_.end());
    next_.store(p.watermark, std::memory_order_relaxed);
    return true;
}

} // namespace ex369
