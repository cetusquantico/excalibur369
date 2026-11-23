// EXCALIBUR369 - SHA-256 (scalar reference).
#pragma once
#include <cstdint>
#include <cstring>

namespace ex369 {

struct Sha256 {
    uint32_t h[8];
    uint64_t len;
    uint8_t  buf[64];
    size_t   buflen;

    void init();
    void update(const uint8_t* p, size_t n);
    void final(uint8_t out[32]);
};

void sha256(const uint8_t* p, size_t n, uint8_t out[32]);
// Specialized: hash exactly 33 bytes (a compressed pubkey) in one block.
void sha256_33(const uint8_t in[33], uint8_t out[32]);

} // namespace ex369

namespace ex369 {
// Benchmark-only: the original straightforward w[64] formulation, kept so that
// scalar variants can be compared head-to-head instead of by assertion.
void sha256_33_ref(const uint8_t in[33], uint8_t out[32]);
}
