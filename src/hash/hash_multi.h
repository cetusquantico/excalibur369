// EXCALIBUR369 - multi-buffer HASH160 with runtime ISA dispatch.
//
// Every backend computes, for `lanes` compressed pubkeys at once:
//     RIPEMD160(SHA256(pubkey))
// The scalar backend (lanes == 1) is the reference; SIMD backends must agree
// with it bit-for-bit (tests/test_hash_simd.cpp enforces this).
#pragma once
#include <cstdint>
#include "../cpu/cpu.h"

namespace ex369 {

struct MultiHash {
    const char* name = "scalar";
    int lanes = 1;
    // in:  lanes * 33 bytes, lane L at in + L*33
    // out: lanes * 20 bytes, lane L at out + L*20
    void (*hash160)(const uint8_t* in, uint8_t* out) = nullptr;
};

// Highest-throughput backend this CPU can actually execute (honours a forced
// override such as "scalar" or "avx2").
const MultiHash& multihash_for(SimdBackend want);
// All backends compiled into this binary, usable or not (for benchmarking).
int multihash_all(const MultiHash** out, int max);

} // namespace ex369
