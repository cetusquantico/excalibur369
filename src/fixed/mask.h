// EXCALIBUR369 - fixed-byte / fixed-bit engine.
//
// Model: the search space is
//     S = { k in [range_lo, range_hi] : (k & mask) == (value & mask) }
//
// The engine works at BIT level (a fixed byte is simply 8 pinned bits), which
// makes future partial-bit masks a configuration change rather than a rewrite.
//
// Free bits (those not pinned by the mask and not pinned by the range prefix)
// are collected LSB-first and grouped into maximal contiguous runs.  An
// enumeration index i in [0, 2^F) is scattered across those free bits to form a
// scalar.  The lowest contiguous run gives the inner loop a CONSTANT stride, so
// the incremental P += stride*G / P -= stride*G fast path is preserved.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../ec/u256.h"

namespace ex369 {

using uidx = unsigned __int128;   // enumeration index (supports up to 127 free bits)

std::string uidx_to_dec(uidx v);
bool uidx_from_dec(const std::string& s, uidx& out);

enum class MaskClass {
    CONTIGUOUS_FAST,  // free bits are one run starting at bit 0  -> stride 1  (P += G)
    STRIDED,          // free bits are one run starting at bit b  -> stride 2^b (P += S)
    GENERIC           // several runs -> inner run is strided, outer needs a fresh kG per block
};
const char* mask_class_name(MaskClass c);

struct BitRun {
    int start;   // lowest bit index of the run (0 = LSB of the 256-bit scalar)
    int len;     // number of bits in the run
};

// A fully-resolved enumeration space: range prefix + mask + value.
struct KeySpace {
    U256 range_lo, range_hi;
    uint8_t mask[32]  = {0};   // big-endian bytes; 0xFF = fully pinned byte
    uint8_t value[32] = {0};

    // --- derived ---
    U256 base;                 // scalar with every pinned bit already set
    std::vector<BitRun> runs;  // maximal free-bit runs, LSB-first
    int  free_bits = 0;        // F = sum of run lengths
    MaskClass klass = MaskClass::CONTIGUOUS_FAST;
    U256 inner_stride;         // scalar delta for +1 on the inner index
    int  inner_bits = 0;       // length of the lowest run (inner loop width)
    bool range_is_aligned = true;  // true when [lo,hi] is exactly the prefix sub-cube

    // Total enumeration indices = 2^free_bits (may exceed 64 bits).
    uidx total_indices() const {
        return ((uidx)1) << free_bits;
    }
    uidx inner_count() const { return ((uidx)1) << inner_bits; }
    uidx outer_count() const {
        int ob = free_bits - inner_bits;
        return ((uidx)1) << ob;
    }

    // Recompute all derived fields. Returns false on an unusable configuration.
    bool finalize(std::string& err);

    // index -> scalar (scatter free bits). Correct for every mask class.
    U256 scalar_at(uidx index) const;

    // Split an index into (outer, inner) halves for the block engine.
    uidx outer_of(uidx index) const { return index >> inner_bits; }
    uidx inner_of(uidx index) const { return index & (inner_count() - 1); }
    // Scalar at the start of an outer block (inner index == 0).
    U256 block_base(uidx outer) const { return scalar_at(outer << inner_bits); }

    bool in_range(const U256& k) const { return k >= range_lo && k <= range_hi; }

    std::string describe() const;
};

// One fixed byte position, resolved against a concrete range.
//
// A fixed byte can straddle the range prefix: for puzzle 71 the top active byte
// (big-endian byte 23) holds bits 64..71, but only bits 64..69 are free - bits
// 70..71 are dictated by the range itself.  Such a byte therefore carries only
// 2^6 distinct values, not 2^8, and its top bits must be taken from the range.
// Ignoring this would (a) clobber the prefix and emit out-of-range keys, and
// (b) make rotation revisit the same keyspace 4 times over.
struct FixedSlot {
    int     byte = 0;          // absolute big-endian byte index [0,31]
    int     free_bits = 8;     // how many of its 8 bits are inside the range
    uint8_t prefix_value = 0;  // the byte's prefix-dictated high bits
    uint8_t free_mask() const { return (uint8_t)((1u << free_bits) - 1u); }
    uint64_t value_count() const { return 1ULL << free_bits; }
};

// Build mask/value bytes from a CLI-level fixed-byte configuration.
struct FixedConfig {
    bool enabled = false;
    int  count = 0;                    // number of fixed bytes (auto positions)
    std::vector<int> positions;        // byte positions (see position_base)
    std::vector<uint8_t> values;       // manual values; empty => rotated
    bool positions_auto = true;
    bool values_auto = true;
    bool absolute_positions = false;   // false => positions relative to the active window
};

// Resolve `positions` into absolute big-endian byte indices [0,31] for a range.
bool resolve_positions(const FixedConfig& cfg, const U256& lo, const U256& hi,
                       std::vector<int>& out_abs, std::string& err);

// Resolve positions and annotate each with its free-bit width and prefix bits.
bool resolve_slots(const FixedConfig& cfg, const U256& lo, const U256& hi,
                   std::vector<FixedSlot>& out, std::string& err);

// Number of bits below the range's common prefix (the range-free region).
int range_free_bits(const U256& lo, const U256& hi);

// Index of the first big-endian byte that is not pinned by the range prefix.
int active_first_byte(const U256& lo, const U256& hi);

} // namespace ex369
