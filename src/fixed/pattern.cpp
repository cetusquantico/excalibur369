#include "pattern.h"
#include <cstdio>
#include <sstream>

namespace ex369 {

bool PatternSpace::init(const FixedConfig& cfg, const std::vector<FixedSlot>& slots,
                        Order rotation, uint64_t seed, std::string& err) {
    enabled_ = cfg.enabled;
    slots_ = slots;
    rot_ = rotation;
    if (!enabled_) { count_ = 1; return true; }
    if (slots_.empty()) { err = "fixed bytes enabled but no positions resolved"; return false; }

    if (!cfg.values.empty()) {
        if (cfg.values.size() != slots_.size()) {
            err = "--fixed-values has " + std::to_string(cfg.values.size()) +
                  " entries but " + std::to_string(slots_.size()) + " positions";
            return false;
        }
        // A manual value must agree with the bits the range itself dictates.
        for (size_t i = 0; i < slots_.size(); i++) {
            const FixedSlot& s = slots_[i];
            uint8_t v = cfg.values[i];
            if ((uint8_t)(v & ~s.free_mask()) != s.prefix_value) {
                char b[192];
                snprintf(b, sizeof(b),
                    "fixed value %02X at byte %d is outside the range: only the low %d bit(s) "
                    "are free there, so the byte must be %02X..%02X",
                    v, s.byte, s.free_bits, s.prefix_value,
                    (uint8_t)(s.prefix_value | s.free_mask()));
                err = b;
                return false;
            }
        }
        manual_ = cfg.values;
        manual_values_ = true;
        count_ = 1;
        return true;
    }

    // Automatic values: rotate over the product of the slots' free-bit spaces.
    int total_bits = 0;
    for (const FixedSlot& s : slots_) total_bits += s.free_bits;
    if (total_bits > 56) {
        err = "automatic fixed-value rotation is capped at 56 bits of pattern space (" +
              std::to_string(total_bits) + " requested); pass explicit --fixed-values";
        return false;
    }
    count_ = 1ULL << total_bits;
    if (rot_ == Order::Random) prp_.init((uidx)count_, seed ^ 0xA5A5C3C31E1E7070ULL);
    return true;
}

std::vector<int> PatternSpace::positions() const {
    std::vector<int> v;
    for (const FixedSlot& s : slots_) v.push_back(s.byte);
    return v;
}

uint64_t PatternSpace::ordinal_at(uint64_t slot) const {
    if (count_ <= 1) return 0;
    switch (rot_) {
        case Order::Sequential: return slot;
        case Order::Reverse:    return count_ - 1 - slot;
        default:                return (uint64_t)prp_.at((uidx)slot);
    }
}

void PatternSpace::materialize(uint64_t ordinal, uint8_t mask[32], uint8_t value[32]) const {
    for (int i = 0; i < 32; i++) { mask[i] = 0; value[i] = 0; }
    if (!enabled_) return;

    if (manual_values_) {
        for (size_t i = 0; i < slots_.size(); i++) {
            mask[slots_[i].byte]  = 0xFF;
            value[slots_[i].byte] = manual_[i];
        }
        return;
    }
    // Mixed-radix split of `ordinal`; the LAST slot is the fastest-moving digit
    // so that sequential rotation walks the least significant fixed byte first.
    uint64_t rest = ordinal;
    for (size_t i = slots_.size(); i-- > 0;) {
        const FixedSlot& s = slots_[i];
        uint64_t radix = s.value_count();
        uint8_t digit = (uint8_t)(rest % radix);
        rest /= radix;
        mask[s.byte]  = 0xFF;
        value[s.byte] = (uint8_t)(s.prefix_value | digit);
    }
}

std::string PatternSpace::describe(uint64_t ordinal) const {
    if (!enabled_) return "fixed:off";
    uint8_t m[32], v[32];
    materialize(ordinal, m, v);
    std::ostringstream o;
    o << "fixed{";
    for (size_t i = 0; i < slots_.size(); i++) {
        if (i) o << ",";
        char b[16];
        snprintf(b, sizeof(b), "%02X", v[slots_[i].byte]);
        o << "b" << slots_[i].byte << "=" << b;
    }
    o << "}";
    return o.str();
}

} // namespace ex369
