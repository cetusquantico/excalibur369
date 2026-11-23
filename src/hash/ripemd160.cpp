#include "ripemd160.h"
#include <cstring>

namespace ex369 {

static inline uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static inline uint32_t f1(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
static inline uint32_t f2(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
static inline uint32_t f3(uint32_t x, uint32_t y, uint32_t z) { return (x | ~y) ^ z; }
static inline uint32_t f4(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
static inline uint32_t f5(uint32_t x, uint32_t y, uint32_t z) { return x ^ (y | ~z); }

static const uint8_t RL[80] = {
 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
 7,4,13,1,10,6,15,3,12,0,9,5,2,14,11,8,
 3,10,14,4,9,15,8,1,2,7,0,6,13,11,5,12,
 1,9,11,10,0,8,12,4,13,3,7,15,14,5,6,2,
 4,0,5,9,7,12,2,10,14,1,3,8,11,6,15,13};
static const uint8_t RR[80] = {
 5,14,7,0,9,2,11,4,13,6,15,8,1,10,3,12,
 6,11,3,7,0,13,5,10,14,15,8,12,4,9,1,2,
 15,5,1,3,7,14,6,9,11,8,12,2,10,0,4,13,
 8,6,4,1,3,11,15,0,5,12,2,13,9,7,10,14,
 12,15,10,4,1,5,8,7,6,2,13,14,0,3,9,11};
static const uint8_t SL[80] = {
 11,14,15,12,5,8,7,9,11,13,14,15,6,7,9,8,
 7,6,8,13,11,9,7,15,7,12,15,9,11,7,13,12,
 11,13,6,7,14,9,13,15,14,8,13,6,5,12,7,5,
 11,12,14,15,14,15,9,8,9,14,5,6,8,6,5,12,
 9,15,5,11,6,8,13,12,5,12,13,14,11,8,5,6};
static const uint8_t SR[80] = {
 8,9,9,11,13,15,15,5,7,7,8,11,14,14,12,6,
 9,13,15,7,12,8,9,11,7,7,12,7,6,15,13,11,
 9,7,15,11,8,6,6,14,12,13,5,14,13,13,7,5,
 15,5,8,11,14,14,6,14,6,9,12,9,12,5,15,8,
 8,5,12,9,12,5,14,6,8,13,6,5,15,13,11,11};
static const uint32_t KL[5] = {0x00000000,0x5A827999,0x6ED9EBA1,0x8F1BBCDC,0xA953FD4E};
static const uint32_t KR[5] = {0x50A28BE6,0x5C4DD124,0x6D703EF3,0x7A6D76E9,0x00000000};

static void compress(uint32_t h[5], const uint8_t block[64]) {
    uint32_t x[16];
    for (int i = 0; i < 16; i++)
        x[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1] << 8) |
               ((uint32_t)block[i*4+2] << 16) | ((uint32_t)block[i*4+3] << 24);
    uint32_t al=h[0], bl=h[1], cl=h[2], dl=h[3], el=h[4];
    uint32_t ar=al, br=bl, cr=cl, dr=dl, er=el;
    for (int j = 0; j < 80; j++) {
        int r = j / 16;
        uint32_t t;
        switch (r) {
            case 0: t = f1(bl,cl,dl); break;
            case 1: t = f2(bl,cl,dl); break;
            case 2: t = f3(bl,cl,dl); break;
            case 3: t = f4(bl,cl,dl); break;
            default: t = f5(bl,cl,dl); break;
        }
        t = rol(al + t + x[RL[j]] + KL[r], SL[j]) + el;
        al = el; el = dl; dl = rol(cl, 10); cl = bl; bl = t;
        switch (r) {
            case 0: t = f5(br,cr,dr); break;
            case 1: t = f4(br,cr,dr); break;
            case 2: t = f3(br,cr,dr); break;
            case 3: t = f2(br,cr,dr); break;
            default: t = f1(br,cr,dr); break;
        }
        t = rol(ar + t + x[RR[j]] + KR[r], SR[j]) + er;
        ar = er; er = dr; dr = rol(cr, 10); cr = br; br = t;
    }
    uint32_t tmp = h[1] + cl + dr;
    h[1] = h[2] + dl + er;
    h[2] = h[3] + el + ar;
    h[3] = h[4] + al + br;
    h[4] = h[0] + bl + cr;
    h[0] = tmp;
}

static void out_le(const uint32_t h[5], uint8_t out[20]) {
    for (int i = 0; i < 5; i++) {
        out[i*4+0] = (uint8_t)(h[i]);
        out[i*4+1] = (uint8_t)(h[i] >> 8);
        out[i*4+2] = (uint8_t)(h[i] >> 16);
        out[i*4+3] = (uint8_t)(h[i] >> 24);
    }
}

void ripemd160(const uint8_t* p, size_t n, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
    size_t i = 0;
    for (; i + 64 <= n; i += 64) compress(h, p + i);
    uint8_t tail[128];
    size_t rem = n - i;
    memcpy(tail, p + i, rem);
    tail[rem] = 0x80;
    size_t total = (rem < 56) ? 64 : 128;
    memset(tail + rem + 1, 0, total - rem - 1);
    uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++) tail[total - 8 + k] = (uint8_t)(bits >> (k * 8));
    compress(h, tail);
    if (total == 128) compress(h, tail + 64);
    out_le(h, out);
}

void ripemd160_32(const uint8_t in[32], uint8_t out[20]) {
    uint8_t block[64];
    memcpy(block, in, 32);
    block[32] = 0x80;
    memset(block + 33, 0, 64 - 33);
    block[56] = 0x00; block[57] = 0x01;   // 256 bits, little-endian
    uint32_t h[5] = {0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0};
    compress(h, block);
    out_le(h, out);
}

} // namespace ex369
