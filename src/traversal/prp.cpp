#include "prp.h"
#include <stdexcept>

namespace ex369 {

uint64_t splitmix64(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

uint64_t Prp::mix(uint64_t x, uint64_t k) {
    x += k;
    x ^= x >> 33;
    x *= 0xFF51AFD7ED558CCDULL;
    x ^= x >> 33;
    x *= 0xC4CEB9FE1A85EC53ULL;
    x ^= x >> 33;
    return x;
}

void Prp::init(uidx domain, uint64_t seed, int rounds) {
    if (domain > ((uidx)1 << 126)) throw std::invalid_argument("PRP domain exceeds 126 bits");
    domain_ = domain ? domain : 1;
    rounds_ = rounds < 4 ? 4 : rounds;
    identity_ = false;

    // Smallest number of bits that covers the domain.
    int need = 0;
    uidx t = domain_ - 1;
    while (t) { need++; t >>= 1; }
    if (need == 0) { identity_ = true; return; }     // domain == 1

    // Round up to an even width so the Feistel halves are balanced.
    half_ = (need + 1) / 2;
    if (half_ > 63) half_ = 63;                      // 126-bit domain ceiling
    bits_ = half_ * 2;
    hmask_ = (half_ >= 64) ? ~0ULL : ((1ULL << half_) - 1);
    pow2_  = ((uidx)1 << bits_);

    uint64_t st = seed ^ 0x3691A5C7D2E4B8F0ULL;
    for (int i = 0; i < 8; i++) k_[i] = splitmix64(st);
}

// Balanced Feistel on `bits_` bits: (L,R) -> (R, L ^ F(R)). Bijective for any F.
uidx Prp::feistel(uidx x) const {
    uint64_t L = (uint64_t)(x >> half_) & hmask_;
    uint64_t R = (uint64_t)x & hmask_;
    for (int i = 0; i < rounds_; i++) {
        uint64_t nL = R;
        uint64_t nR = (L ^ mix(R, k_[i & 7])) & hmask_;
        L = nL;
        R = nR;
    }
    return ((uidx)L << half_) | (uidx)R;
}

uidx Prp::at(uidx i) const {
    if (identity_) return i;
    uidx x = i & (pow2_ - 1);
    // Cycle walking: iterate the permutation until the image lands in the domain.
    // Still a bijection on [0, domain), so no repeats and no omissions.
    for (;;) {
        x = feistel(x);
        if (x < domain_) return x;
    }
}

} // namespace ex369
