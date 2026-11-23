#include "stats.h"
#include <cstdio>
#include <sstream>

namespace ex369 {

std::string human_rate(double v) {
    const char* u[] = {"", "K", "M", "G", "T"};
    int i = 0;
    while (v >= 1000.0 && i < 4) { v /= 1000.0; i++; }
    char b[64];
    snprintf(b, sizeof(b), "%.3f %s", v, u[i]);
    return b;
}

std::string candidate_progress_note(uint64_t candidates, uint64_t outside_range_segments) {
    if (candidates == 0 && outside_range_segments > 0)
        return "no candidates reported in this run; discarding out-of-range segments";
    return "";
}

std::string BenchReport::format(const std::string& label) const {
    std::ostringstream o;
    char b[128];
    o << "=== " << label << " ===\n";
    snprintf(b, sizeof(b), "  elapsed            : %.3f s\n", seconds);            o << b;
    snprintf(b, sizeof(b), "  threads            : %d\n", threads);                o << b;
    o << "  CANDIDATES/s       : " << human_rate(candidates_per_sec()) << "\n";
    o << "  candidates/core/s  : " << human_rate(per_core_per_sec()) << "\n";
    o << "  EC points/s        : " << human_rate(seconds > 0 ? ec_points / seconds : 0) << "\n";
    o << "  SHA256/s           : " << human_rate(seconds > 0 ? sha256_ops / seconds : 0) << "\n";
    o << "  HASH160/s          : " << human_rate(seconds > 0 ? hash160_ops / seconds : 0) << "\n";
    snprintf(b, sizeof(b), "  total candidates   : %llu\n", (unsigned long long)candidates); o << b;
    snprintf(b, sizeof(b), "  block kG count     : %llu\n", (unsigned long long)ec_mul_gen); o << b;
    snprintf(b, sizeof(b), "  segments done      : %llu\n", (unsigned long long)segments);   o << b;
    snprintf(b, sizeof(b), "  outside-range segs : %llu\n", (unsigned long long)outside_range_segments); o << b;
    uint64_t with_work = segments >= outside_range_segments ? segments - outside_range_segments : 0;
    snprintf(b, sizeof(b), "  completed with work: %llu\n", (unsigned long long)with_work); o << b;
    auto note = candidate_progress_note(candidates, outside_range_segments);
    if (!note.empty()) o << "  note               : " << note << "\n";
    return o.str();
}

} // namespace ex369
