// EXCALIBUR369 - fixed-byte value rotation ("patterns").
//
// A pattern is one concrete assignment of values to the fixed byte positions.
// Manual values give exactly one pattern; automatic values enumerate the whole
// value space in sequential / reverse / random-no-repeat order.
//
// The value space of a slot is 2^free_bits, NOT always 2^8: a fixed byte that
// straddles the range prefix only has its low bits free (see FixedSlot).  Using
// 2^8 there would revisit the same keys several times over.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "mask.h"
#include "../config.h"
#include "../traversal/prp.h"

namespace ex369 {

class PatternSpace {
public:
    bool init(const FixedConfig& cfg, const std::vector<FixedSlot>& slots,
              Order rotation, uint64_t seed, std::string& err);

    uint64_t count() const { return count_; }
    const std::vector<FixedSlot>& slots() const { return slots_; }
    std::vector<int> positions() const;
    bool enabled() const { return enabled_; }

    // slot index in [0,count) -> actual pattern ordinal, honouring the rotation.
    uint64_t ordinal_at(uint64_t slot) const;
    void materialize(uint64_t ordinal, uint8_t mask[32], uint8_t value[32]) const;
    std::string describe(uint64_t ordinal) const;

private:
    bool                    enabled_ = false;
    std::vector<FixedSlot>  slots_;
    std::vector<uint8_t>    manual_;
    bool                    manual_values_ = false;
    uint64_t                count_ = 1;
    Order                   rot_ = Order::Sequential;
    Prp                     prp_;
};

} // namespace ex369
