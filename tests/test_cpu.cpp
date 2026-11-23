#include "testlib.h"
#include "cpu/cpu.h"
#include "hash/hash_multi.h"
using namespace ex369;

int main() {
    t::section("AVX requires CPU, XSAVE, OSXSAVE and OS-managed vector state");
    for (unsigned flags = 0; flags < 8; ++flags) {
        uint32_t leaf1 = (1u << 9) | ((flags & 1) ? (1u << 26) : 0) |
                         ((flags & 2) ? (1u << 27) : 0) | ((flags & 4) ? (1u << 28) : 0);
        for (unsigned state = 0; state < 256; ++state) {
            CpuInfo cpu;
            decode_x86_features(cpu, leaf1, (1u << 5) | (1u << 16) | (1u << 30) | (1u << 31), state);
            bool avx = flags == 7 && (state & 6) == 6;
            bool zmm = avx && (state & 0xe0) == 0xe0;
            t::check(cpu.avx == avx && cpu.avx2 == avx, "AVX/AVX2 state gate");
            t::check(cpu.avx512f == zmm && cpu.avx512bw == zmm && cpu.avx512vl == zmm, "AVX-512 state gate");
            t::check(cpu.ssse3, "SSSE3 does not require AVX state");
        }
    }
    t::section("selection never exceeds hardware or compiled support");
    for (bool hw_sse : {false, true}) for (bool hw_avx : {false, true})
        for (bool build_sse : {false, true}) for (bool build_avx : {false, true}) {
            CpuInfo cpu; cpu.ssse3 = hw_sse; cpu.avx2 = hw_avx;
            for (const char* request : {"", "scalar", "sse", "avx2"}) {
                auto result = select_backend_for(cpu, request, build_sse, build_avx);
                t::check(result != SimdBackend::AVX2 || (hw_avx && build_avx), "AVX2 must be usable");
                t::check(result != SimdBackend::SSE || (hw_sse && build_sse), "SSE must be usable");
                if (std::string(request) == "scalar") t::check(result == SimdBackend::Scalar, "scalar override");
                if (std::string(request) == "sse") t::check(result <= SimdBackend::SSE, "SSE ceiling");
                auto best = hw_avx && build_avx ? SimdBackend::AVX2 :
                            hw_sse && build_sse ? SimdBackend::SSE : SimdBackend::Scalar;
                if (!*request || std::string(request) == "avx2") t::check(result == best, "best available tier");
            }
        }
    CpuInfo legacy;
    decode_x86_features(legacy, 0, 0, 0);
    t::check(!legacy.avx && !legacy.avx2 && !legacy.ssse3, "legacy CPU");
    t::check(std::string(multihash_for(select_backend("scalar")).name) == "scalar", "real scalar dispatch");
    return t::finish("test_cpu");
}
