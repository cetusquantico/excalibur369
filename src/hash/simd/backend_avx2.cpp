// EXCALIBUR369 - 8-way AVX2 multi-buffer HASH160.
#include <immintrin.h>
#include <cstring>
#include "../hash_multi.h"
#include "vec_ops.h"

#define VEC          __m256i
#define VADD(a,b)    _mm256_add_epi32(a,b)
#define VXOR(a,b)    _mm256_xor_si256(a,b)
#define VAND(a,b)    _mm256_and_si256(a,b)
#define VOR(a,b)     _mm256_or_si256(a,b)
#define VANDN(a,b)   _mm256_andnot_si256(a,b)          /* ~a & b */
#define VNOT(a)      _mm256_xor_si256(a, _mm256_set1_epi32(-1))
#define VSHL(a,n)    _mm256_slli_epi32(a,n)
#define VSHR(a,n)    _mm256_srli_epi32(a,n)
#define VSET1(x)     _mm256_set1_epi32((int)(x))
#define LANES        8

namespace ex369 {
namespace {

static inline __m256i bswap32x8(__m256i v) {
    // vpshufb works per 128-bit lane, so the same pattern is repeated twice.
    const __m256i m = _mm256_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12,
                                       3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);
    return _mm256_shuffle_epi8(v, m);   // vpshufb is per-128-bit-lane
}
static inline uint32_t be32_at(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);            // lanes are 33 bytes apart, so never aligned
    return __builtin_bswap32(v);
}
static inline __m256i load_be_lane_word(const uint8_t* in, int word) {
    // Gather word `word` (big-endian) from each of the 8 lanes' 33-byte inputs.
    return _mm256_setr_epi32((int)be32_at(in + 0 * 33 + word * 4),
                             (int)be32_at(in + 1 * 33 + word * 4),
                             (int)be32_at(in + 2 * 33 + word * 4),
                             (int)be32_at(in + 3 * 33 + word * 4),
                             (int)be32_at(in + 4 * 33 + word * 4),
                             (int)be32_at(in + 5 * 33 + word * 4),
                             (int)be32_at(in + 6 * 33 + word * 4),
                             (int)be32_at(in + 7 * 33 + word * 4));
}

void hash160_x8(const uint8_t* in, uint8_t* out) {
    VEC w[64], st[8];
    for (int i = 0; i < 8; i++) w[i] = load_be_lane_word(in, i);
    // Byte 32 of each lane, then the constant one-block tail (0x80, zeros, 264).
    w[8] = _mm256_setr_epi32(
        (int)(((uint32_t)in[0 * 33 + 32] << 24) | 0x00800000u),
        (int)(((uint32_t)in[1 * 33 + 32] << 24) | 0x00800000u),
        (int)(((uint32_t)in[2 * 33 + 32] << 24) | 0x00800000u),
        (int)(((uint32_t)in[3 * 33 + 32] << 24) | 0x00800000u),
        (int)(((uint32_t)in[4 * 33 + 32] << 24) | 0x00800000u),
        (int)(((uint32_t)in[5 * 33 + 32] << 24) | 0x00800000u),
        (int)(((uint32_t)in[6 * 33 + 32] << 24) | 0x00800000u),
        (int)(((uint32_t)in[7 * 33 + 32] << 24) | 0x00800000u));
    w[9] = w[10] = w[11] = w[12] = w[13] = w[14] = _mm256_setzero_si256();
    w[15] = VSET1(264);
    #include "sha256_core.inc"

    // SHA-256 output bytes are big-endian; RIPEMD-160 reads little-endian words.
    VEC x[16];
    for (int i = 0; i < 8; i++) x[i] = bswap32x8(st[i]);
    x[8]  = VSET1(0x00000080);
    x[9]  = x[10] = x[11] = x[12] = x[13] = _mm256_setzero_si256();
    x[14] = VSET1(256);
    x[15] = _mm256_setzero_si256();
    VEC st5[5];
    {
        VEC* st = st5;
        #include "ripemd160_core.inc"
    }
    uint32_t t[5][LANES];
    for (int i = 0; i < 5; i++) _mm256_storeu_si256((__m256i*)t[i], st5[i]);
    for (int L = 0; L < LANES; L++)
        for (int i = 0; i < 5; i++)
            memcpy(out + L * 20 + i * 4, &t[i][L], 4);   // little-endian words
}

} // namespace

const MultiHash& multihash_avx2() {
    static MultiHash m{"avx2-x8", LANES, &hash160_x8};
    return m;
}

} // namespace ex369
