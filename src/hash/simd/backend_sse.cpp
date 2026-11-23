// EXCALIBUR369 - 4-way SSE2/SSSE3 multi-buffer HASH160.
#include <immintrin.h>
#include <cstring>
#include "../hash_multi.h"
#include "vec_ops.h"

#define VEC          __m128i
#define VADD(a,b)    _mm_add_epi32(a,b)
#define VXOR(a,b)    _mm_xor_si128(a,b)
#define VAND(a,b)    _mm_and_si128(a,b)
#define VOR(a,b)     _mm_or_si128(a,b)
#define VANDN(a,b)   _mm_andnot_si128(a,b)          /* ~a & b */
#define VNOT(a)      _mm_xor_si128(a, _mm_set1_epi32(-1))
#define VSHL(a,n)    _mm_slli_epi32(a,n)
#define VSHR(a,n)    _mm_srli_epi32(a,n)
#define VSET1(x)     _mm_set1_epi32((int)(x))
#define LANES        4

namespace ex369 {
namespace {

static inline __m128i bswap32x4(__m128i v) {
    const __m128i m = _mm_setr_epi8(3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12);
    return _mm_shuffle_epi8(v, m);
}
static inline uint32_t be32_at(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);            // lanes are 33 bytes apart, so never aligned
    return __builtin_bswap32(v);
}
static inline __m128i load_be_lane_word(const uint8_t* in, int word) {
    // Gather word `word` (big-endian) from each of the 4 lanes' 33-byte inputs.
    return _mm_setr_epi32((int)be32_at(in + 0 * 33 + word * 4),
                          (int)be32_at(in + 1 * 33 + word * 4),
                          (int)be32_at(in + 2 * 33 + word * 4),
                          (int)be32_at(in + 3 * 33 + word * 4));
}

void hash160_x4(const uint8_t* in, uint8_t* out) {
    VEC w[64], st[8];
    for (int i = 0; i < 8; i++) w[i] = load_be_lane_word(in, i);
    // Byte 32 of each lane, then the constant one-block tail (0x80, zeros, 264).
    w[8] = _mm_setr_epi32(
        (int)(((uint32_t)in[0 * 33 + 32] << 24) | 0x00800000u),
        (int)(((uint32_t)in[1 * 33 + 32] << 24) | 0x00800000u),
        (int)(((uint32_t)in[2 * 33 + 32] << 24) | 0x00800000u),
        (int)(((uint32_t)in[3 * 33 + 32] << 24) | 0x00800000u));
    w[9] = w[10] = w[11] = w[12] = w[13] = w[14] = _mm_setzero_si128();
    w[15] = VSET1(264);
    #include "sha256_core.inc"

    // SHA-256 output bytes are big-endian; RIPEMD-160 reads little-endian words.
    VEC x[16];
    for (int i = 0; i < 8; i++) x[i] = bswap32x4(st[i]);
    x[8]  = VSET1(0x00000080);
    x[9]  = x[10] = x[11] = x[12] = x[13] = _mm_setzero_si128();
    x[14] = VSET1(256);
    x[15] = _mm_setzero_si128();
    VEC st5[5];
    {
        VEC* st = st5;
        #include "ripemd160_core.inc"
    }
    uint32_t t[5][LANES];
    for (int i = 0; i < 5; i++) _mm_storeu_si128((__m128i*)t[i], st5[i]);
    for (int L = 0; L < LANES; L++)
        for (int i = 0; i < 5; i++)
            memcpy(out + L * 20 + i * 4, &t[i][L], 4);   // little-endian words
}

} // namespace

const MultiHash& multihash_sse() {
    static MultiHash m{"sse-x4", LANES, &hash160_x4};
    return m;
}

} // namespace ex369
