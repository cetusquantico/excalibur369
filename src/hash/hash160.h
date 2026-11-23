// EXCALIBUR369 - HASH160 = RIPEMD160(SHA256(x)).
#pragma once
#include "sha256.h"
#include "ripemd160.h"

namespace ex369 {
inline void hash160(const uint8_t* p, size_t n, uint8_t out[20]) {
    uint8_t s[32];
    sha256(p, n, s);
    ripemd160(s, 32, out);
}
// Hot-loop variant for a 33-byte compressed pubkey.
inline void hash160_pubkey33(const uint8_t in[33], uint8_t out[20]) {
    uint8_t s[32];
    sha256_33(in, s);
    ripemd160_32(s, out);
}
} // namespace ex369
