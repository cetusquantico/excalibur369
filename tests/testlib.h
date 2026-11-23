#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace t {
inline int g_fail = 0, g_pass = 0;
inline std::string g_suite;

inline void check(bool ok, const std::string& what) {
    if (ok) { g_pass++; }
    else { g_fail++; printf("  FAIL: %s\n", what.c_str()); }
}
inline void eq_str(const std::string& a, const std::string& b, const std::string& what) {
    if (a == b) g_pass++;
    else { g_fail++; printf("  FAIL: %s\n    got      %s\n    expected %s\n",
                            what.c_str(), a.c_str(), b.c_str()); }
}
inline void eq_u64(unsigned long long a, unsigned long long b, const std::string& what) {
    if (a == b) g_pass++;
    else { g_fail++; printf("  FAIL: %s\n    got %llu expected %llu\n", what.c_str(), a, b); }
}
inline void section(const std::string& s) { printf("[%s]\n", s.c_str()); }
inline int finish(const std::string& suite) {
    printf("%s: %d passed, %d failed\n", suite.c_str(), g_pass, g_fail);
    return g_fail ? 1 : 0;
}
} // namespace t
