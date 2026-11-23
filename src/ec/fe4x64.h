// EXCALIBUR369 - secp256k1 base field Fp, p = 2^256 - 2^32 - 977.
// SATURATED 4 x 64-bit limbs, values kept fully reduced in [0, p).
//
// This is the original reference representation. It is correct and simple, but
// every add/sub/mul carries through a serialized adc chain, which on a core
// without ADX (Ivy Bridge here) costs ~92 cycles per multiply. See fe5x52.h for
// the unsaturated alternative and docs/BENCHMARKS.md for the comparison.
//
// Selected with -DEX369_FIELD=4x64.
#pragma once
#include <cstdint>
#include <cstring>
#include "u256.h"

namespace ex369 {

// 2^256 mod p == 0x1000003D1
static constexpr uint64_t FE_R = 0x1000003D1ULL;

struct Fe {
    uint64_t n[4]; // little-endian limbs, always in [0, p)

    Fe() { n[0] = n[1] = n[2] = n[3] = 0; }
    explicit Fe(uint64_t v) { n[0] = v; n[1] = n[2] = n[3] = 0; }

    bool is_zero() const { return (n[0] | n[1] | n[2] | n[3]) == 0; }
    bool is_odd() const { return n[0] & 1; }
};

static const uint64_t FE_P[4] = {
    0xFFFFFFFEFFFFFC2FULL, 0xFFFFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL
};

inline bool fe_eq(const Fe& a, const Fe& b) {
    return ((a.n[0] ^ b.n[0]) | (a.n[1] ^ b.n[1]) | (a.n[2] ^ b.n[2]) | (a.n[3] ^ b.n[3])) == 0;
}

// r = a - p if a >= p, else a. Branch-free-ish conditional subtract.
inline void fe_reduce_once(uint64_t r[4]) {
    // compare r >= p
    bool ge;
    if (r[3] != FE_P[3]) ge = r[3] > FE_P[3];
    else if (r[2] != FE_P[2]) ge = r[2] > FE_P[2];
    else if (r[1] != FE_P[1]) ge = r[1] > FE_P[1];
    else ge = r[0] >= FE_P[0];
    if (ge) {
        unsigned __int128 bor = 0;
        for (int i = 0; i < 4; i++) {
            unsigned __int128 t = (unsigned __int128)r[i] - FE_P[i] - (uint64_t)bor;
            r[i] = (uint64_t)t;
            bor = (t >> 64) & 1;
        }
    }
}

inline void fe_add(Fe& r, const Fe& a, const Fe& b) {
    unsigned __int128 c = 0;
    uint64_t t[4];
    for (int i = 0; i < 4; i++) {
        c += (unsigned __int128)a.n[i] + b.n[i];
        t[i] = (uint64_t)c;
        c >>= 64;
    }
    if ((uint64_t)c) {
        // overflowed 2^256: add R (since 2^256 == R mod p)
        unsigned __int128 cc = FE_R;
        for (int i = 0; i < 4; i++) {
            cc += t[i];
            t[i] = (uint64_t)cc;
            cc >>= 64;
        }
    }
    fe_reduce_once(t);
    memcpy(r.n, t, 32);
}

inline void fe_sub(Fe& r, const Fe& a, const Fe& b) {
    uint64_t t[4];
    unsigned __int128 bor = 0;
    for (int i = 0; i < 4; i++) {
        unsigned __int128 x = (unsigned __int128)a.n[i] - b.n[i] - (uint64_t)bor;
        t[i] = (uint64_t)x;
        bor = (x >> 64) & 1;
    }
    if ((uint64_t)bor) {
        // add p back
        unsigned __int128 c = 0;
        for (int i = 0; i < 4; i++) {
            c += (unsigned __int128)t[i] + FE_P[i];
            t[i] = (uint64_t)c;
            c >>= 64;
        }
    }
    memcpy(r.n, t, 32);
}

inline void fe_neg(Fe& r, const Fe& a) {
    if (a.is_zero()) { r = Fe(); return; }
    uint64_t t[4];
    unsigned __int128 bor = 0;
    for (int i = 0; i < 4; i++) {
        unsigned __int128 x = (unsigned __int128)FE_P[i] - a.n[i] - (uint64_t)bor;
        t[i] = (uint64_t)x;
        bor = (x >> 64) & 1;
    }
    memcpy(r.n, t, 32);
}

// Fold a 512-bit product (8 limbs) into Fp.
inline void fe_reduce512(Fe& r, const uint64_t w[8]) {
    // Pass 1: lo + hi*R  (hi = w[4..7], R fits in 33 bits) -> 5 limbs
    uint64_t t[5];
    unsigned __int128 c = 0;
    for (int i = 0; i < 4; i++) {
        c += (unsigned __int128)w[i] + (unsigned __int128)w[4 + i] * FE_R;
        t[i] = (uint64_t)c;
        c >>= 64;
    }
    t[4] = (uint64_t)c;

    // Pass 2: fold the 5th limb back in.
    c = (unsigned __int128)t[4] * FE_R;
    uint64_t u[4];
    for (int i = 0; i < 4; i++) {
        c += t[i];
        u[i] = (uint64_t)c;
        c >>= 64;
    }
    // Pass 3: at most a tiny carry remains.
    if ((uint64_t)c) {
        unsigned __int128 cc = (unsigned __int128)(uint64_t)c * FE_R;
        for (int i = 0; i < 4; i++) {
            cc += u[i];
            u[i] = (uint64_t)cc;
            cc >>= 64;
        }
        if ((uint64_t)cc) {
            unsigned __int128 c3 = (unsigned __int128)(uint64_t)cc * FE_R;
            for (int i = 0; i < 4; i++) {
                c3 += u[i];
                u[i] = (uint64_t)c3;
                c3 >>= 64;
            }
        }
    }
    fe_reduce_once(u);
    memcpy(r.n, u, 32);
}

// 256x256 -> 512-bit schoolbook product, then fold mod p.
//
// Benchmarked alternative: a Comba (product-scanning) variant with a 3-limb
// register accumulator measured 97.2 cyc vs 91.8 cyc for this form on Ivy
// Bridge / gcc 15 - i.e. slightly WORSE - so the simple loop stays. The cost
// here is dominated by the adc carry chains, which is a property of the
// saturated 4x64 representation rather than of the loop shape; see
// docs/BENCHMARKS.md for the measured breakdown and the 5x52 plan.
inline void fe_mul(Fe& r, const Fe& a, const Fe& b) {
    uint64_t w[8];
    memset(w, 0, sizeof(w));
    for (int i = 0; i < 4; i++) {
        unsigned __int128 carry = 0;
        for (int j = 0; j < 4; j++) {
            unsigned __int128 t = (unsigned __int128)a.n[i] * b.n[j] + w[i + j] + carry;
            w[i + j] = (uint64_t)t;
            carry = t >> 64;
        }
        w[i + 4] = (uint64_t)carry;
    }
    fe_reduce512(r, w);
}

// Exposed for benchmarking: the product alone, without the modular fold.
inline void fe_mul512_only(uint64_t w[8], const Fe& a, const Fe& b) {
    memset(w, 0, sizeof(uint64_t) * 8);
    for (int i = 0; i < 4; i++) {
        unsigned __int128 carry = 0;
        for (int j = 0; j < 4; j++) {
            unsigned __int128 t = (unsigned __int128)a.n[i] * b.n[j] + w[i + j] + carry;
            w[i + j] = (uint64_t)t;
            carry = t >> 64;
        }
        w[i + 4] = (uint64_t)carry;
    }
}

// Measured: a dedicated 10-multiply squaring was indistinguishable from
// fe_mul(a,a) end-to-end here (6.72 vs 6.74 Mcand/s, inside the noise floor),
// so the simpler form ships. Revisit if fe_mul ever gets an asm path.
inline void fe_sqr(Fe& r, const Fe& a) { fe_mul(r, a, a); }

// Inversion by Fermat: a^(p-2). Fixed addition chain (libsecp256k1 style).
inline void fe_inv(Fe& r, const Fe& a) {
    Fe x2, x3, x6, x9, x11, x22, x44, x88, x176, x220, x223, t1;
    fe_sqr(x2, a);      fe_mul(x2, x2, a);          // a^3    (2 bits)
    fe_sqr(x3, x2);     fe_mul(x3, x3, a);          // a^7    (3 bits)
    t1 = x3;
    for (int i = 0; i < 3; i++) fe_sqr(t1, t1);
    fe_mul(x6, t1, x3);                              // 6 bits
    t1 = x6;
    for (int i = 0; i < 3; i++) fe_sqr(t1, t1);
    fe_mul(x9, t1, x3);                              // 9 bits
    t1 = x9;
    for (int i = 0; i < 2; i++) fe_sqr(t1, t1);
    fe_mul(x11, t1, x2);                             // 11 bits
    t1 = x11;
    for (int i = 0; i < 11; i++) fe_sqr(t1, t1);
    fe_mul(x22, t1, x11);                            // 22 bits
    t1 = x22;
    for (int i = 0; i < 22; i++) fe_sqr(t1, t1);
    fe_mul(x44, t1, x22);                            // 44 bits
    t1 = x44;
    for (int i = 0; i < 44; i++) fe_sqr(t1, t1);
    fe_mul(x88, t1, x44);                            // 88 bits
    t1 = x88;
    for (int i = 0; i < 88; i++) fe_sqr(t1, t1);
    fe_mul(x176, t1, x88);                           // 176 bits
    t1 = x176;
    for (int i = 0; i < 44; i++) fe_sqr(t1, t1);
    fe_mul(x220, t1, x44);                           // 220 bits
    t1 = x220;
    for (int i = 0; i < 3; i++) fe_sqr(t1, t1);
    fe_mul(x223, t1, x3);                            // 223 bits

    // p-2 = 0x...FFFFFC2D : 223 ones, then the tail.
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

// sqrt via a^((p+1)/4); caller must verify r^2 == a.
inline void fe_sqrt(Fe& r, const Fe& a) {
    // (p+1)/4 = 0x3FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFBFFFFF0C
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

inline void fe_to_be32(const Fe& a, uint8_t out[32]) {
    for (int i = 0; i < 4; i++) {
        uint64_t w = a.n[3 - i];
        out[i*8+0]=(uint8_t)(w>>56); out[i*8+1]=(uint8_t)(w>>48);
        out[i*8+2]=(uint8_t)(w>>40); out[i*8+3]=(uint8_t)(w>>32);
        out[i*8+4]=(uint8_t)(w>>24); out[i*8+5]=(uint8_t)(w>>16);
        out[i*8+6]=(uint8_t)(w>>8);  out[i*8+7]=(uint8_t)(w);
    }
}
// Parse 32 big-endian bytes, reducing mod p (input need not be < p).
inline Fe fe_from_be32_mod(const uint8_t in[32]);

inline Fe fe_from_be32(const uint8_t in[32]) {
    Fe r;
    for (int i = 0; i < 4; i++) {
        uint64_t w = 0;
        for (int j = 0; j < 8; j++) w = (w << 8) | in[i * 8 + j];
        r.n[3 - i] = w;
    }
    return r;
}

inline Fe fe_from_be32_mod(const uint8_t in[32]) {
    Fe r = fe_from_be32(in);
    // 2^256 - p < p, so at most two conditional subtractions are ever needed.
    fe_reduce_once(r.n);
    fe_reduce_once(r.n);
    return r;
}

// No-ops here: 4x64 values are always canonical.
inline void fe_normalize(Fe&) {}
inline void fe_normalize_weak(Fe&) {}

} // namespace ex369
