// EXCALIBUR369 - CPU capability detection and thread affinity.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ex369 {

struct CpuInfo {
    std::string vendor, brand;
    int  logical_threads = 1;
    int  physical_cores  = 1;
    bool smt             = false;
    bool ssse3 = false, sse42 = false, avx = false, avx2 = false;
    bool avx512f = false, avx512bw = false, avx512vl = false;
    bool bmi1 = false, bmi2 = false, adx = false, sha_ni = false;
    uint32_t l1d_kb = 0, l2_kb = 0, l3_kb = 0;
    std::string summary() const;
};

const CpuInfo& cpu_info();

// Pin the calling thread to a logical CPU. Returns false if unsupported/failed.
bool pin_thread_to_cpu(int cpu);
// Preferred CPU list: one per physical core first, then SMT siblings.
std::vector<int> affinity_plan(int nthreads, bool avoid_smt);

// Ordered by width. SSE is a real tier, not a fallback: on pre-AVX2 cores it is
// the difference between 1-lane and 4-lane hashing.
enum class SimdBackend { Scalar, SSE, AVX2, AVX512 };
const char* simd_name(SimdBackend b);
// Best backend actually usable on this CPU (respects a forced override).
SimdBackend select_backend(const char* forced);
// Pure capability checks, also used by tests with synthetic CPUID/OS state.
void decode_x86_features(CpuInfo& out, uint32_t leaf1_ecx, uint32_t leaf7_ebx, uint64_t xcr0);
SimdBackend select_backend_for(const CpuInfo& cpu, const char* forced,
                               bool compiled_sse, bool compiled_avx2);
std::string compiled_backends();

} // namespace ex369
