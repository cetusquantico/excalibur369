#include "cpu.h"
#include <cstring>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace ex369 {

static std::vector<int> allowed_cpus() {
    std::vector<int> result;
#ifdef __linux__
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0,sizeof(allowed),&allowed)==0) {
        for (int cpu=0; cpu<CPU_SETSIZE; cpu++)
            if (CPU_ISSET(cpu,&allowed)) result.push_back(cpu);
    }
    if (result.empty()) {
        long n=sysconf(_SC_NPROCESSORS_ONLN);
        for (int cpu=0; cpu<n && cpu<CPU_SETSIZE; cpu++) result.push_back(cpu);
    }
#endif
    if (result.empty()) result.push_back(0);
    return result;
}

static void cpuid_ex(uint32_t leaf, uint32_t sub, uint32_t r[4]) {
#if defined(__x86_64__) || defined(__i386__)
    __cpuid_count(leaf, sub, r[0], r[1], r[2], r[3]);
#else
    r[0]=r[1]=r[2]=r[3]=0;
#endif
}

static uint64_t read_xcr0() {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t eax, edx;
    __asm__ volatile(".byte 0x0f,0x01,0xd0" : "=a"(eax), "=d"(edx) : "c"(0));
    return (uint64_t(edx) << 32) | eax;
#else
    return 0;
#endif
}

void decode_x86_features(CpuInfo& c, uint32_t ecx, uint32_t ebx, uint64_t xcr0) {
    c.ssse3 = (ecx & (1u << 9)) != 0;
    c.sse42 = (ecx & (1u << 20)) != 0;
    constexpr uint32_t required = (1u << 26) | (1u << 27) | (1u << 28);
    c.avx = (ecx & required) == required && (xcr0 & 0x6) == 0x6;
    c.avx2 = c.avx && (ebx & (1u << 5));
    bool zmm = c.avx && (xcr0 & 0xe0) == 0xe0;
    c.avx512f = zmm && (ebx & (1u << 16));
    c.avx512bw = c.avx512f && (ebx & (1u << 30));
    c.avx512vl = c.avx512f && (ebx & (1u << 31));
    c.bmi1 = (ebx & (1u << 3)) != 0;
    c.bmi2 = (ebx & (1u << 8)) != 0;
    c.adx = (ebx & (1u << 19)) != 0;
    c.sha_ni = (ebx & (1u << 29)) != 0;
}

static uint32_t read_sysfs_kb(const char* path) {
    std::ifstream f(path);
    if (!f) return 0;
    std::string s;
    f >> s;
    if (s.empty()) return 0;
    uint32_t v = (uint32_t)strtoul(s.c_str(), nullptr, 10);
    if (s.find('M') != std::string::npos) v *= 1024;
    return v;
}

static void detect_topology(CpuInfo& c) {
#ifdef __linux__
    const auto cpus = allowed_cpus();
    c.logical_threads = (int)cpus.size();
    // Count distinct (physical_package_id, core_id) pairs.
    std::set<std::pair<int,int>> cores;
    for (int i : cpus) {
        char p1[256], p2[256];
        snprintf(p1, sizeof(p1), "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);
        snprintf(p2, sizeof(p2), "/sys/devices/system/cpu/cpu%d/topology/core_id", i);
        std::ifstream f1(p1), f2(p2);
        int pkg = 0, core = i;
        if (f1) f1 >> pkg;
        if (f2) f2 >> core;
        cores.insert({pkg, core});
    }
    c.physical_cores = cores.empty() ? c.logical_threads : (int)cores.size();
    c.smt = c.logical_threads > c.physical_cores;
    c.l1d_kb = read_sysfs_kb("/sys/devices/system/cpu/cpu0/cache/index0/size");
    c.l2_kb  = read_sysfs_kb("/sys/devices/system/cpu/cpu0/cache/index2/size");
    c.l3_kb  = read_sysfs_kb("/sys/devices/system/cpu/cpu0/cache/index3/size");
#else
    c.logical_threads = 1; c.physical_cores = 1;
#endif
}

static CpuInfo detect() {
    CpuInfo c;
    uint32_t r[4];
    cpuid_ex(0, 0, r);
    uint32_t maxleaf = r[0];
    char vend[13];
    memcpy(vend + 0, &r[1], 4); memcpy(vend + 4, &r[3], 4); memcpy(vend + 8, &r[2], 4);
    vend[12] = 0;
    c.vendor = vend;

    char brand[49] = {0};
    cpuid_ex(0x80000000, 0, r);
    if (r[0] >= 0x80000004) {
        for (int i = 0; i < 3; i++) {
            cpuid_ex(0x80000002 + i, 0, r);
            memcpy(brand + i * 16, r, sizeof(r));
        }
        c.brand = brand;
        while (!c.brand.empty() && c.brand.front() == ' ') c.brand.erase(c.brand.begin());
    }

    uint32_t ecx = 0, ebx = 0;
    if (maxleaf >= 1) { cpuid_ex(1, 0, r); ecx = r[2]; }
    if (maxleaf >= 7) {
        cpuid_ex(7, 0, r);
        ebx = r[1];
    }
    constexpr uint32_t required = (1u << 26) | (1u << 27) | (1u << 28);
    decode_x86_features(c, ecx, ebx, (ecx & required) == required ? read_xcr0() : 0);
    detect_topology(c);
    return c;
}

const CpuInfo& cpu_info() {
    static CpuInfo c = detect();
    return c;
}

std::string CpuInfo::summary() const {
    std::ostringstream o;
    o << brand << "\n";
    o << "  cores=" << physical_cores << " threads=" << logical_threads
      << " smt=" << (smt ? "yes" : "no") << "\n";
    o << "  isa:";
    auto p = [&](const char* n, bool v) { if (v) o << " " << n; };
    p("ssse3", ssse3); p("sse4.2", sse42); p("avx", avx); p("avx2", avx2);
    p("avx512f", avx512f); p("avx512bw", avx512bw); p("avx512vl", avx512vl);
    p("bmi1", bmi1); p("bmi2", bmi2); p("adx", adx); p("sha-ni", sha_ni);
    o << "\n  cache: L1d=" << l1d_kb << "KB L2=" << l2_kb << "KB L3=" << l3_kb << "KB";
    return o.str();
}

bool pin_thread_to_cpu(int cpu) {
#ifdef __linux__
    if (cpu < 0 || cpu >= CPU_SETSIZE) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
    (void)cpu; return false;
#endif
}

std::vector<int> affinity_plan(int nthreads, bool avoid_smt) {
    const CpuInfo& c = cpu_info();
    std::vector<int> order;
#ifdef __linux__
    // Group logical CPUs by (pkg, core); take one sibling per core first.
    std::vector<std::pair<std::pair<int,int>, int>> v;
    for (int i : allowed_cpus()) {
        char p1[256], p2[256];
        snprintf(p1, sizeof(p1), "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);
        snprintf(p2, sizeof(p2), "/sys/devices/system/cpu/cpu%d/topology/core_id", i);
        std::ifstream f1(p1), f2(p2);
        int pkg = 0, core = i;
        if (f1) f1 >> pkg;
        if (f2) f2 >> core;
        v.push_back({{pkg, core}, i});
    }
    std::set<std::pair<int,int>> seen;
    for (auto& e : v) if (seen.insert(e.first).second) order.push_back(e.second);
    if (!avoid_smt) {
        std::set<int> have(order.begin(), order.end());
        for (auto& e : v) if (!have.count(e.second)) order.push_back(e.second);
    }
#endif
    if (order.empty()) for (int i = 0; i < c.logical_threads; i++) order.push_back(i);
    std::vector<int> out;
    for (int i = 0; i < nthreads; i++) out.push_back(order[i % order.size()]);
    return out;
}

const char* simd_name(SimdBackend b) {
    switch (b) {
        case SimdBackend::AVX512: return "avx512";
        case SimdBackend::AVX2:   return "avx2";
        case SimdBackend::SSE:    return "sse";
        default:                  return "scalar";
    }
}

SimdBackend select_backend_for(const CpuInfo& c, const char* forced,
                               bool compiled_sse, bool compiled_avx2) {
    SimdBackend ceiling = SimdBackend::AVX2;
    if (forced && *forced) {
        if (!strcmp(forced, "scalar")) return SimdBackend::Scalar;
        if (!strcmp(forced, "sse")) ceiling = SimdBackend::SSE;
    }
    if (ceiling >= SimdBackend::AVX2 && compiled_avx2 && c.avx2) return SimdBackend::AVX2;
    if (compiled_sse && c.ssse3) return SimdBackend::SSE;
    return SimdBackend::Scalar;
}

SimdBackend select_backend(const char* forced) {
    bool sse = false, avx2 = false;
#ifdef EX369_HAVE_AVX2
    avx2 = true;
#endif
#ifdef EX369_HAVE_SSE
    sse = true;
#endif
    return select_backend_for(cpu_info(), forced, sse, avx2);
}

std::string compiled_backends() {
    std::string result = "scalar";
#ifdef EX369_HAVE_SSE
    result += " sse-x4";
#endif
#ifdef EX369_HAVE_AVX2
    result += " avx2-x8";
#endif
    return result;
}

} // namespace ex369
