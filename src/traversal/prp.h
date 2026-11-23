// EXCALIBUR369 - random-no-repeat ordering.
//
// A keyed Feistel network gives a BIJECTION on [0, 2^bits).  Because it is a
// bijection, visiting permute(0), permute(1), ... can never repeat and can never
// skip an element - the "no-repeat" guarantee is structural, not bookkeeping,
// and costs O(1) memory instead of a visited-set of size 2^bits.
//
// For domains that are not a power of two we cycle-walk: re-permute until the
// image lands inside the domain.  Cycle walking preserves bijectivity.
#pragma once
#include <cstdint>
#include "../fixed/mask.h"

namespace ex369 {

class Prp {
public:
    Prp() = default;
    // domain: number of elements. seed: reproducible key.
    void init(uidx domain, uint64_t seed, int rounds = 6);

    uidx domain() const { return domain_; }
    // i-th element of the permuted sequence (i < domain).
    uidx at(uidx i) const;

    bool identity() const { return identity_; }

private:
    uidx     domain_ = 1;
    uidx     pow2_   = 1;   // 2^bits_, the Feistel's own domain
    int      bits_   = 0;   // always even
    int      half_   = 0;   // bits_ / 2
    uint64_t hmask_  = 0;   // (1<<half_)-1
    uint64_t k_[8]   = {0};
    int      rounds_ = 6;
    bool     identity_ = true;

    uidx feistel(uidx x) const;
    static uint64_t mix(uint64_t x, uint64_t k);
};

// Deterministic 64-bit stream from a seed (used for automatic fixed values etc).
uint64_t splitmix64(uint64_t& state);

} // namespace ex369
