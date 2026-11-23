#include "sha256.h"

namespace ex369 {

static const uint32_t K[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

static inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static inline uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Ch/Maj in their cheaper algebraic forms (one operation fewer each).
#define Ch(x,y,z)  ((z) ^ ((x) & ((y) ^ (z))))
#define Maj(x,y,z) (((x) & (y)) | ((z) & ((x) | (y))))
#define S0(x) (ror(x,2)  ^ ror(x,13) ^ ror(x,22))
#define S1(x) (ror(x,6)  ^ ror(x,11) ^ ror(x,25))
#define s0(x) (ror(x,7)  ^ ror(x,18) ^ ((x) >> 3))
#define s1(x) (ror(x,17) ^ ror(x,19) ^ ((x) >> 10))

// Rotating-register round: no shuffling of a..h between rounds.
#define RND(a,b,c,d,e,f,g,h,ki,wi) do {              \
    uint32_t t1 = (h) + S1(e) + Ch(e,f,g) + (ki) + (wi); \
    uint32_t t2 = S0(a) + Maj(a,b,c);                \
    (d) += t1;                                       \
    (h)  = t1 + t2;                                  \
} while (0)

// One round-group of 8, consuming w[i..i+7].
#define R8(i) \
    RND(a,b,c,d,e,f,g,h, K[i+0], w[i+0]); \
    RND(h,a,b,c,d,e,f,g, K[i+1], w[i+1]); \
    RND(g,h,a,b,c,d,e,f, K[i+2], w[i+2]); \
    RND(f,g,h,a,b,c,d,e, K[i+3], w[i+3]); \
    RND(e,f,g,h,a,b,c,d, K[i+4], w[i+4]); \
    RND(d,e,f,g,h,a,b,c, K[i+5], w[i+5]); \
    RND(c,d,e,f,g,h,a,b, K[i+6], w[i+6]); \
    RND(b,c,d,e,f,g,h,a, K[i+7], w[i+7])

// Circular 16-word message expansion: w[i] += s0(w[i+1]) + w[i+9] + s1(w[i+14]),
// all indices mod 16. Later lines deliberately read words updated by earlier
// ones - that is what makes the in-place circular schedule correct.
#define EXPAND16() do { \
    w[ 0] += s0(w[ 1]) + w[ 9] + s1(w[14]); \
    w[ 1] += s0(w[ 2]) + w[10] + s1(w[15]); \
    w[ 2] += s0(w[ 3]) + w[11] + s1(w[ 0]); \
    w[ 3] += s0(w[ 4]) + w[12] + s1(w[ 1]); \
    w[ 4] += s0(w[ 5]) + w[13] + s1(w[ 2]); \
    w[ 5] += s0(w[ 6]) + w[14] + s1(w[ 3]); \
    w[ 6] += s0(w[ 7]) + w[15] + s1(w[ 4]); \
    w[ 7] += s0(w[ 8]) + w[ 0] + s1(w[ 5]); \
    w[ 8] += s0(w[ 9]) + w[ 1] + s1(w[ 6]); \
    w[ 9] += s0(w[10]) + w[ 2] + s1(w[ 7]); \
    w[10] += s0(w[11]) + w[ 3] + s1(w[ 8]); \
    w[11] += s0(w[12]) + w[ 4] + s1(w[ 9]); \
    w[12] += s0(w[13]) + w[ 5] + s1(w[10]); \
    w[13] += s0(w[14]) + w[ 6] + s1(w[11]); \
    w[14] += s0(w[15]) + w[ 7] + s1(w[12]); \
    w[15] += s0(w[ 0]) + w[ 8] + s1(w[13]); \
} while (0)

// Benchmark note: an unrolled, rotating-register variant of this function with
// an in-place 16-word circular schedule measured 17% SLOWER here (1468 vs 1259
// cycles/block on Ivy Bridge, gcc 15). The straightforward form below lets the
// compiler schedule better, so that is what ships. Re-measure before changing.
static inline void sha256_rounds(uint32_t h[8], const uint32_t w[64]) {
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + S1(e) + Ch(e,f,g) + K[i] + w[i];
        uint32_t t2 = S0(a) + Maj(a,b,c);
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}

static void compress(uint32_t h[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = be32(block + i * 4);
    for (int i = 16; i < 64; i++)
        w[i] = w[i-16] + s0(w[i-15]) + w[i-7] + s1(w[i-2]);
    sha256_rounds(h, w);
}

void Sha256::init() {
    h[0]=0x6a09e667; h[1]=0xbb67ae85; h[2]=0x3c6ef372; h[3]=0xa54ff53a;
    h[4]=0x510e527f; h[5]=0x9b05688c; h[6]=0x1f83d9ab; h[7]=0x5be0cd19;
    len = 0; buflen = 0;
}

void Sha256::update(const uint8_t* p, size_t n) {
    len += n;
    while (n) {
        size_t take = 64 - buflen;
        if (take > n) take = n;
        memcpy(buf + buflen, p, take);
        buflen += take; p += take; n -= take;
        if (buflen == 64) { compress(h, buf); buflen = 0; }
    }
}

void Sha256::final(uint8_t out[32]) {
    uint64_t bitlen = len * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t z = 0;
    while (buflen != 56) update(&z, 1);
    uint8_t l[8];
    for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bitlen >> (56 - i * 8));
    len -= 8;   // the length field itself is not part of the message
    update(l, 8);
    for (int i = 0; i < 8; i++) {
        out[i*4+0] = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)(h[i]);
    }
}

void sha256(const uint8_t* p, size_t n, uint8_t out[32]) {
    Sha256 s; s.init(); s.update(p, n); s.final(out);
}

// Hot path: a 33-byte compressed pubkey is exactly one block whose tail is
// constant (0x80 pad, zeros, bit length 264), so the schedule is built straight
// from the input - no block assembly, no memset.
void sha256_33(const uint8_t in[33], uint8_t out[32]) {
    uint32_t w[64];
    w[0] = be32(in + 0);  w[1] = be32(in + 4);  w[2] = be32(in + 8);  w[3] = be32(in + 12);
    w[4] = be32(in + 16); w[5] = be32(in + 20); w[6] = be32(in + 24); w[7] = be32(in + 28);
    w[8] = ((uint32_t)in[32] << 24) | 0x00800000u;
    w[9] = w[10] = w[11] = w[12] = w[13] = w[14] = 0;
    w[15] = 264;
    for (int i = 16; i < 64; i++)
        w[i] = w[i-16] + s0(w[i-15]) + w[i-7] + s1(w[i-2]);
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    sha256_rounds(h, w);
    for (int i = 0; i < 8; i++) {
        out[i*4+0] = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)(h[i]);
    }
}

} // namespace ex369

namespace ex369 {
// Reference formulation: precompute the full 64-word schedule, then loop.
void sha256_33_ref(const uint8_t in[33], uint8_t out[32]) {
    uint8_t block[64];
    memcpy(block, in, 33);
    block[33] = 0x80;
    memset(block + 34, 0, 64 - 34);
    block[62] = 0x01;
    block[63] = 0x08;
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = be32(block + i * 4);
    for (int i = 16; i < 64; i++)
        w[i] = w[i-16] + s0(w[i-15]) + w[i-7] + s1(w[i-2]);
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + S1(e) + Ch(e,f,g) + K[i] + w[i];
        uint32_t t2 = S0(a) + Maj(a,b,c);
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    for (int i = 0; i < 8; i++) {
        out[i*4+0]=(uint8_t)(h[i]>>24); out[i*4+1]=(uint8_t)(h[i]>>16);
        out[i*4+2]=(uint8_t)(h[i]>>8);  out[i*4+3]=(uint8_t)(h[i]);
    }
}
}
