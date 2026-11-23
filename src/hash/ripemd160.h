// EXCALIBUR369 - RIPEMD-160 (scalar reference).
#pragma once
#include <cstdint>
#include <cstddef>

namespace ex369 {
void ripemd160(const uint8_t* p, size_t n, uint8_t out[20]);
// Specialized: digest exactly 32 bytes (a SHA-256 output) in one block.
void ripemd160_32(const uint8_t in[32], uint8_t out[20]);
} // namespace ex369
