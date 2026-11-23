#include "self_test.h"
#include "build_info.h"
#include "hash/hash_multi.h"
#include "hash/hash160.h"
#include "verify/verify.h"
#include <cstring>
#include <sstream>
#include <vector>

namespace ex369 {
std::string build_description() {
    std::ostringstream out;
    out << "EXCALIBUR369 " << EX369_BUILD_VERSION << "\n"
        << "  compiler: " << EX369_BUILD_COMPILER << "\n"
        << "  system: " << EX369_BUILD_SYSTEM << "\n"
        << "  build type: " << EX369_BUILD_TYPE << "\n"
        << "  field: " << EX369_BUILD_FIELD << "\n"
        << "  pgo: " << (*EX369_BUILD_PGO ? EX369_BUILD_PGO : "OFF") << "\n"
        << "  native: " << (EX369_NATIVE ? "ON (local CPU only)" : "OFF") << "\n"
        << "  lto: " << (EX369_LTO ? "ON (SIMD objects isolated)" : "OFF") << "\n"
        << "  compiled backends: " << compiled_backends();
    return out.str();
}

bool self_test(std::string& report) {
    std::ostringstream out;
    bool ok = true;
    auto check = [&](bool passed, const char* label) {
        out << (passed ? "PASS " : "FAIL ") << label << "\n";
        ok = ok && passed;
    };
    uint8_t digest[32];
    sha256(reinterpret_cast<const uint8_t*>("abc"), 3, digest);
    check(to_hex(digest, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known vector");
    ripemd160(reinterpret_cast<const uint8_t*>("abc"), 3, digest);
    check(to_hex(digest, 20) == "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc", "RIPEMD-160 known vector");
    struct Vector { const char* key; const char* address; const char* hash; };
    for (const auto& v : {
        Vector{"1", "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH", "751e76e8199196d454941c45d1b3a323f1433bd6"},
        Vector{"4b5f8303e9", "122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT8", "0b304f2a79a027270276533fe1ed4eff30910876"}}) {
        U256 k;
        bool parsed = U256::from_hex(v.key, k);
        VerifyResult r = verify_candidate(k, nullptr);
        check(parsed && r.ok && r.address == v.address && r.hash160_hex == v.hash, v.address);
    }
    const MultiHash* backends[8];
    int count = multihash_all(backends, 8);
    for (int b = 0; b < count; ++b) {
        const auto& mh = *backends[b];
        bool same = true;
        std::vector<uint8_t> input(size_t(mh.lanes) * 33), got(size_t(mh.lanes) * 20), expected(got.size());
        for (int trial = 0; trial < 16; ++trial) {
            for (size_t i = 0; i < input.size(); ++i) input[i] = uint8_t(i * 73 + trial * 19);
            for (int lane = 0; lane < mh.lanes; ++lane)
                hash160(input.data() + size_t(lane) * 33, 33, expected.data() + size_t(lane) * 20);
            mh.hash160(input.data(), got.data());
            same = same && got == expected;
        }
        check(same, mh.name);
    }
    out << "Self-test " << (ok ? "PASSED" : "FAILED") << "; only usable compiled backends were executed.\n";
    report = out.str();
    return ok;
}
} // namespace ex369
