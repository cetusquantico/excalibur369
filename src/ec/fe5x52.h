// EXCALIBUR369 - secp256k1 base field Fp, p = 2^256 - 2^32 - 977.
// UNSATURATED 5 x 52-bit limbs.
//
// Why this exists: with saturated 4x64 limbs every add/sub/mul propagates a
// serialized `adc` chain.  On a core without ADX that chain, not the multiplier,
// is the bottleneck (measured: the 512-bit product alone cost 60 of fe_mul's 92
// cycles, ~3.8 cycles per mulq).  Leaving 12 spare bits in each 64-bit limb lets
// partial products accumulate inside a 128-bit register with no carry chain at
// all; carries are paid once, at the end.
//
//   value = n[0] + n[1]*2^52 + n[2]*2^104 + n[3]*2^156 + n[4]*2^208
//
// INVARIANT for every stored element ("weakly normalized"):
//     n[0..3] <= 2^52 - 1 ,  n[4] <= 2^48 ,  so the value is < 2^256.
// The value may still be >= p.  Exactly two values are congruent to 0 under
// this invariant (0 and p), because 2p > 2^256 - that fact is what lets
// is_zero() stay cheap.  Anything that needs a canonical value (serialization,
// parity, equality) normalizes a copy first.
//
// Selected with -DEX369_FIELD=5x52.
#pragma once
#include <cstdint>
#include <cstring>
#include "u256.h"

namespace ex369 {

static constexpr uint64_t FE_M52 = 0xFFFFFFFFFFFFFULL;   // 2^52 - 1
static constexpr uint64_t FE_M48 = 0x0FFFFFFFFFFFFULL;   // 2^48 - 1
static constexpr uint64_t FE_R   = 0x1000003D1ULL;       // 2^256 mod p
static constexpr uint64_t FE_R4  = 0x1000003D10ULL;      // 2^260 mod p == 16*R

// p in this representation.
static constexpr uint64_t FE_P0 = 0xFFFFEFFFFFC2FULL;
static constexpr uint64_t FE_P1 = 0xFFFFFFFFFFFFFULL;
static constexpr uint64_t FE_P4 = 0x0FFFFFFFFFFFFULL;

struct Fe {
    uint64_t n[5];

    Fe() { n[0] = n[1] = n[2] = n[3] = n[4] = 0; }
    explicit Fe(uint64_t v) {
        n[0] = v & FE_M52;
        n[1] = v >> 52;
        n[2] = n[3] = n[4] = 0;
    }
    bool is_zero() const;
    bool is_odd() const;
};

// Carry propagation only: restores the invariant, does NOT reduce below p.
inline void fe_normalize_weak(Fe& r) {
    uint64_t t0 = r.n[0], t1 = r.n[1], t2 = r.n[2], t3 = r.n[3], t4 = r.n[4];
    // Fold whatever sits at or above 2^256 back in via 2^256 == R (mod p).
    uint64_t over = t4 >> 48;
    t4 &= FE_M48;
    t0 += over * FE_R;
    t1 += t0 >> 52; t0 &= FE_M52;
    t2 += t1 >> 52; t1 &= FE_M52;
    t3 += t2 >> 52; t2 &= FE_M52;
    t4 += t3 >> 52; t3 &= FE_M52;
    // t4 <= 2^48 now, so the value is < 2^256 as the invariant requires.
    r.n[0] = t0; r.n[1] = t1; r.n[2] = t2; r.n[3] = t3; r.n[4] = t4;
}

// Full reduction to the canonical representative in [0, p).
//
// PRECONDITION: r already satisfies the invariant (every operation in this file
// ends by restoring it, so that holds for any stored element).  Re-running the
// weak pass here would be pure waste, and the hot loop calls this twice per
// candidate - once for the parity bit, once to serialize x.
inline void fe_normalize(Fe& r) {
    uint64_t t0 = r.n[0], t1 = r.n[1], t2 = r.n[2], t3 = r.n[3], t4 = r.n[4];
    // value >= p iff it overflowed 2^256, or it matches p's pattern from the top.
    uint64_t ge = (t4 >> 48)
                | ((t4 == FE_P4) & ((t1 & t2 & t3) == FE_M52) & (t0 >= FE_P0));
    if (ge) {
        // Subtracting p mod 2^256 is the same as adding R and dropping bit 256.
        t0 += FE_R;
        t1 += t0 >> 52; t0 &= FE_M52;
        t2 += t1 >> 52; t1 &= FE_M52;
        t3 += t2 >> 52; t2 &= FE_M52;
        t4 += t3 >> 52; t3 &= FE_M52;
        t4 &= FE_M48;
    }
    r.n[0] = t0; r.n[1] = t1; r.n[2] = t2; r.n[3] = t3; r.n[4] = t4;
}

inline bool Fe::is_zero() const {
    // Under the invariant only 0 and p are congruent to zero (2p > 2^256).
    if ((n[0] | n[1] | n[2] | n[3] | n[4]) == 0) return true;
    return n[0] == FE_P0 && n[1] == FE_M52 && n[2] == FE_M52
        && n[3] == FE_M52 && n[4] == FE_P4;
}

inline bool Fe::is_odd() const {
    Fe t = *this;
    fe_normalize(t);
    return t.n[0] & 1;
}

inline bool fe_eq(const Fe& a, const Fe& b) {
    Fe x = a, y = b;
    fe_normalize(x);
    fe_normalize(y);
    return ((x.n[0] ^ y.n[0]) | (x.n[1] ^ y.n[1]) | (x.n[2] ^ y.n[2])
          | (x.n[3] ^ y.n[3]) | (x.n[4] ^ y.n[4])) == 0;
}

inline void fe_add(Fe& r, const Fe& a, const Fe& b) {
    // No carry chain: limbs have 12 spare bits, so this cannot overflow 64 bits.
    r.n[0] = a.n[0] + b.n[0];
    r.n[1] = a.n[1] + b.n[1];
    r.n[2] = a.n[2] + b.n[2];
    r.n[3] = a.n[3] + b.n[3];
    r.n[4] = a.n[4] + b.n[4];
    fe_normalize_weak(r);
}

inline void fe_neg(Fe& r, const Fe& a) {
    // r = 2p - a, which is >= 0 limb-wise because every 2p limb exceeds the
    // invariant's bound on the corresponding limb of a.
    r.n[0] = 2 * FE_P0 - a.n[0];
    r.n[1] = 2 * FE_P1 - a.n[1];
    r.n[2] = 2 * FE_P1 - a.n[2];
    r.n[3] = 2 * FE_P1 - a.n[3];
    r.n[4] = 2 * FE_P4 - a.n[4];
    fe_normalize_weak(r);
}

inline void fe_sub(Fe& r, const Fe& a, const Fe& b) {
    r.n[0] = a.n[0] + 2 * FE_P0 - b.n[0];
    r.n[1] = a.n[1] + 2 * FE_P1 - b.n[1];
    r.n[2] = a.n[2] + 2 * FE_P1 - b.n[2];
    r.n[3] = a.n[3] + 2 * FE_P1 - b.n[3];
    r.n[4] = a.n[4] + 2 * FE_P4 - b.n[4];
    fe_normalize_weak(r);
}

// fe_mul / fe_sqr are written as a SINGLE interleaved pass.
//
// The obvious version materializes all nine 128-bit columns first and then
// folds them.  That needs ~18 registers just for the columns, so the compiler
// spills to the stack and the result measured SLOWER than the 4x64 code it was
// meant to beat (113 vs 92 cycles).  Computing the high columns into u0..u4
// first (five plain 64-bit words) and then folding them into the low columns
// one at a time keeps a single 128-bit accumulator live.
//
// Bounds, with n[0..3] <= 2^52-1 and n[4] <= 2^48+1:
//   each column   < 2^107      (at most 5 products of < 2^104, or 2^105 doubled)
//   u0..u3        < 2^52,  u4 < 2^55
//   u_k * R4      < 2^92        (R4 is 37 bits)
//   the running accumulator never exceeds ~2^107, well inside 128 bits.

// Common tail: fold the part at or above 2^256 back in and restore the invariant.
#define EX369_FE_FINISH(acc, n0, n1, n2, n3, n4)                       \
    do {                                                               \
        unsigned __int128 _t = (acc) * FE_R + (n0);                    \
        (n0) = (uint64_t)_t & FE_M52;                                  \
        (n1) += (uint64_t)(_t >> 52);                                  \
        (n2) += (n1) >> 52; (n1) &= FE_M52;                            \
        (n3) += (n2) >> 52; (n2) &= FE_M52;                            \
        (n4) += (n3) >> 52; (n3) &= FE_M52;                            \
    } while (0)

inline void fe_mul(Fe& r, const Fe& a, const Fe& b) {
    typedef unsigned __int128 u128;
    const uint64_t a0 = a.n[0], a1 = a.n[1], a2 = a.n[2], a3 = a.n[3], a4 = a.n[4];
    const uint64_t b0 = b.n[0], b1 = b.n[1], b2 = b.n[2], b3 = b.n[3], b4 = b.n[4];

    // High columns 5..8, reduced to 52-bit pieces before they get scaled by R4
    // (scaling them unreduced would need ~2^144 and overflow).
    u128 d = (u128)a1 * b4 + (u128)a2 * b3 + (u128)a3 * b2 + (u128)a4 * b1;
    uint64_t u0 = (uint64_t)d & FE_M52; d >>= 52;
    d += (u128)a2 * b4 + (u128)a3 * b3 + (u128)a4 * b2;
    uint64_t u1 = (uint64_t)d & FE_M52; d >>= 52;
    d += (u128)a3 * b4 + (u128)a4 * b3;
    uint64_t u2 = (uint64_t)d & FE_M52; d >>= 52;
    d += (u128)a4 * b4;
    uint64_t u3 = (uint64_t)d & FE_M52; d >>= 52;
    uint64_t u4 = (uint64_t)d;

    // Low columns, folding the high half in as we go: 2^260 == R4 (mod p), so
    // high limb k lands exactly on low limb k.
    u128 acc = (u128)a0 * b0 + (u128)u0 * FE_R4;
    uint64_t n0 = (uint64_t)acc & FE_M52; acc >>= 52;
    acc += (u128)a0 * b1 + (u128)a1 * b0 + (u128)u1 * FE_R4;
    uint64_t n1 = (uint64_t)acc & FE_M52; acc >>= 52;
    acc += (u128)a0 * b2 + (u128)a1 * b1 + (u128)a2 * b0 + (u128)u2 * FE_R4;
    uint64_t n2 = (uint64_t)acc & FE_M52; acc >>= 52;
    acc += (u128)a0 * b3 + (u128)a1 * b2 + (u128)a2 * b1 + (u128)a3 * b0
         + (u128)u3 * FE_R4;
    uint64_t n3 = (uint64_t)acc & FE_M52; acc >>= 52;
    acc += (u128)a0 * b4 + (u128)a1 * b3 + (u128)a2 * b2 + (u128)a3 * b1
         + (u128)a4 * b0 + (u128)u4 * FE_R4;
    uint64_t n4 = (uint64_t)acc & FE_M48; acc >>= 48;

    EX369_FE_FINISH(acc, n0, n1, n2, n3, n4);
    r.n[0] = n0; r.n[1] = n1; r.n[2] = n2; r.n[3] = n3; r.n[4] = n4;
}

inline void fe_sqr(Fe& r, const Fe& a) {
    typedef unsigned __int128 u128;
    const uint64_t a0 = a.n[0], a1 = a.n[1], a2 = a.n[2], a3 = a.n[3], a4 = a.n[4];
    // Cross terms appear twice, so 15 multiplies instead of 25.
    const uint64_t d0 = a0 * 2, d1 = a1 * 2, d2 = a2 * 2, d3 = a3 * 2;

    u128 d = (u128)d1 * a4 + (u128)d2 * a3;
    uint64_t u0 = (uint64_t)d & FE_M52; d >>= 52;
    d += (u128)d2 * a4 + (u128)a3 * a3;
    uint64_t u1 = (uint64_t)d & FE_M52; d >>= 52;
    d += (u128)d3 * a4;
    uint64_t u2 = (uint64_t)d & FE_M52; d >>= 52;
    d += (u128)a4 * a4;
    uint64_t u3 = (uint64_t)d & FE_M52; d >>= 52;
    uint64_t u4 = (uint64_t)d;

    u128 acc = (u128)a0 * a0 + (u128)u0 * FE_R4;
    uint64_t n0 = (uint64_t)acc & FE_M52; acc >>= 52;
    acc += (u128)d0 * a1 + (u128)u1 * FE_R4;
    uint64_t n1 = (uint64_t)acc & FE_M52; acc >>= 52;
    acc += (u128)d0 * a2 + (u128)a1 * a1 + (u128)u2 * FE_R4;
    uint64_t n2 = (uint64_t)acc & FE_M52; acc >>= 52;
    acc += (u128)d0 * a3 + (u128)d1 * a2 + (u128)u3 * FE_R4;
    uint64_t n3 = (uint64_t)acc & FE_M52; acc >>= 52;
    acc += (u128)d0 * a4 + (u128)d1 * a3 + (u128)a2 * a2 + (u128)u4 * FE_R4;
    uint64_t n4 = (uint64_t)acc & FE_M48; acc >>= 48;

    EX369_FE_FINISH(acc, n0, n1, n2, n3, n4);
    r.n[0] = n0; r.n[1] = n1; r.n[2] = n2; r.n[3] = n3; r.n[4] = n4;
}

// --- serialization ----------------------------------------------------------

inline void fe_to_be32(const Fe& a, uint8_t out[32]) {
    Fe t = a;
    fe_normalize(t);
    uint64_t d0 = t.n[0] | (t.n[1] << 52);
    uint64_t d1 = (t.n[1] >> 12) | (t.n[2] << 40);
    uint64_t d2 = (t.n[2] >> 24) | (t.n[3] << 28);
    uint64_t d3 = (t.n[3] >> 36) | (t.n[4] << 16);
    const uint64_t w[4] = {d3, d2, d1, d0};
    for (int i = 0; i < 4; i++) {
        uint64_t v = w[i];
        out[i*8+0]=(uint8_t)(v>>56); out[i*8+1]=(uint8_t)(v>>48);
        out[i*8+2]=(uint8_t)(v>>40); out[i*8+3]=(uint8_t)(v>>32);
        out[i*8+4]=(uint8_t)(v>>24); out[i*8+5]=(uint8_t)(v>>16);
        out[i*8+6]=(uint8_t)(v>>8);  out[i*8+7]=(uint8_t)(v);
    }
}

inline Fe fe_from_be32(const uint8_t in[32]) {
    uint64_t w[4];
    for (int i = 0; i < 4; i++) {
        uint64_t v = 0;
        for (int j = 0; j < 8; j++) v = (v << 8) | in[i * 8 + j];
        w[3 - i] = v;                       // w[0] = least significant word
    }
    Fe r;
    r.n[0] = w[0] & FE_M52;
    r.n[1] = ((w[0] >> 52) | (w[1] << 12)) & FE_M52;
    r.n[2] = ((w[1] >> 40) | (w[2] << 24)) & FE_M52;
    r.n[3] = ((w[2] >> 28) | (w[3] << 36)) & FE_M52;
    r.n[4] = w[3] >> 16;
    return r;
}

inline Fe fe_from_be32_mod(const uint8_t in[32]) {
    Fe r = fe_from_be32(in);
    fe_normalize(r);
    return r;
}

// --- inversion and square root ----------------------------------------------
// Identical addition chains to the 4x64 build; they only use fe_mul/fe_sqr.

inline void fe_inv(Fe& r, const Fe& a) {
    Fe x2, x3, x6, x9, x11, x22, x44, x88, x176, x220, x223, t1;
    fe_sqr(x2, a);      fe_mul(x2, x2, a);
    fe_sqr(x3, x2);     fe_mul(x3, x3, a);
    t1 = x3;
    for (int i = 0; i < 3; i++) fe_sqr(t1, t1);
    fe_mul(x6, t1, x3);
    t1 = x6;
    for (int i = 0; i < 3; i++) fe_sqr(t1, t1);
    fe_mul(x9, t1, x3);
    t1 = x9;
    for (int i = 0; i < 2; i++) fe_sqr(t1, t1);
    fe_mul(x11, t1, x2);
    t1 = x11;
    for (int i = 0; i < 11; i++) fe_sqr(t1, t1);
    fe_mul(x22, t1, x11);
    t1 = x22;
    for (int i = 0; i < 22; i++) fe_sqr(t1, t1);
    fe_mul(x44, t1, x22);
    t1 = x44;
    for (int i = 0; i < 44; i++) fe_sqr(t1, t1);
    fe_mul(x88, t1, x44);
    t1 = x88;
    for (int i = 0; i < 88; i++) fe_sqr(t1, t1);
    fe_mul(x176, t1, x88);
    t1 = x176;
    for (int i = 0; i < 44; i++) fe_sqr(t1, t1);
    fe_mul(x220, t1, x44);
    t1 = x220;
    for (int i = 0; i < 3; i++) fe_sqr(t1, t1);
    fe_mul(x223, t1, x3);

    t1 = x223;
    for (int i = 0; i < 23; i++) fe_sqr(t1, t1);
    fe_mul(t1, t1, x22);
    for (int i = 0; i < 5; i++) fe_sqr(t1, t1);
    fe_mul(t1, t1, a);
    for (int i = 0; i < 3; i++) fe_sqr(t1, t1);
    fe_mul(t1, t1, x2);
    for (int i = 0; i < 2; i++) fe_sqr(t1, t1);
    fe_mul(r, t1, a);
}

inline void fe_sqrt(Fe& r, const Fe& a) {
    // (p+1)/4
    static const uint64_t e[4] = {
        0xFFFFFFFFBFFFFF0CULL, 0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFFFFFFFFFULL, 0x3FFFFFFFFFFFFFFFULL
    };
    Fe acc(1), base = a;
    for (int i = 0; i < 256; i++) {
        if ((e[i >> 6] >> (i & 63)) & 1) fe_mul(acc, acc, base);
        fe_sqr(base, base);
    }
    r = acc;
}

} // namespace ex369
