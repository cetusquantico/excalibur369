// EXCALIBUR369 - vector abstraction for multi-buffer hashing.
//
// The hashing cores are written ONCE against these macros and instantiated per
// ISA (SSE2 4-way, AVX2 8-way, AVX-512 16-way).  Defining a new backend means
// defining ~10 macros, not rewriting SHA-256 or RIPEMD-160.
#pragma once
#include <cstdint>

#define VROL(x, n) VOR(VSHL(x, n), VSHR(x, 32 - (n)))

// --- SHA-256 pieces ---
#define V_CH(x, y, z)  VXOR(z, VAND(x, VXOR(y, z)))
#define V_MAJ(x, y, z) VOR(VAND(x, y), VAND(z, VOR(x, y)))
#define V_S0(x) VXOR(VXOR(VROL(x, 30), VROL(x, 19)), VROL(x, 10))  // ror 2,13,22
#define V_S1(x) VXOR(VXOR(VROL(x, 26), VROL(x, 21)), VROL(x, 7))   // ror 6,11,25
#define V_s0(x) VXOR(VXOR(VROL(x, 25), VROL(x, 14)), VSHR(x, 3))   // ror 7,18 >>3
#define V_s1(x) VXOR(VXOR(VROL(x, 15), VROL(x, 13)), VSHR(x, 10))  // ror17,19 >>10

// --- RIPEMD-160 boolean functions ---
#define V_F1(x, y, z) VXOR(VXOR(x, y), z)
#define V_F2(x, y, z) VOR(VAND(x, y), VANDN(x, z))
#define V_F3(x, y, z) VXOR(VOR(x, VNOT(y)), z)
#define V_F4(x, y, z) VOR(VAND(x, z), VANDN(z, y))
#define V_F5(x, y, z) VXOR(x, VOR(y, VNOT(z)))
