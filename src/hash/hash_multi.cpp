#include "hash_multi.h"
#include "hash160.h"
#include <cstring>

namespace ex369 {

// Reference backend: one lane, the scalar path every SIMD backend is checked against.
static void hash160_x1(const uint8_t* in, uint8_t* out) {
    hash160_pubkey33(in, out);
}
static const MultiHash& multihash_scalar() {
    static MultiHash m{"scalar", 1, &hash160_x1};
    return m;
}

#if defined(EX369_HAVE_SSE)
const MultiHash& multihash_sse();
#endif
#if defined(EX369_HAVE_AVX2)
const MultiHash& multihash_avx2();
#endif

const MultiHash& multihash_for(SimdBackend want) {
    const CpuInfo& c = cpu_info();
    if (want == SimdBackend::Scalar) return multihash_scalar();
    // Ask for the widest usable backend at or below the requested tier: a
    // request for AVX2 on a pre-AVX2 core still gets 4-lane SSE, not 1-lane.
#if defined(EX369_HAVE_AVX2)
    if (c.avx2 && want >= SimdBackend::AVX2) return multihash_avx2();
#endif
#if defined(EX369_HAVE_SSE)
    if (c.ssse3 && want >= SimdBackend::SSE) return multihash_sse();
#endif
    (void)c;
    return multihash_scalar();
}

int multihash_all(const MultiHash** out, int max) {
    int n = 0;
    if (n < max) out[n++] = &multihash_scalar();
#if defined(EX369_HAVE_SSE)
    if (n < max && cpu_info().ssse3) out[n++] = &multihash_sse();
#endif
#if defined(EX369_HAVE_AVX2)
    if (n < max && cpu_info().avx2) out[n++] = &multihash_avx2();
#endif
    return n;
}

} // namespace ex369
