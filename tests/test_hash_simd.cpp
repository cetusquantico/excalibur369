// Every multi-buffer backend must agree with the scalar reference BIT FOR BIT.
// Only backends this CPU can actually execute are exercised; the rest are
// reported as untested rather than silently assumed correct.
#include "testlib.h"
#include "hash/hash_multi.h"
#include "hash/hash160.h"
#include "ec/group.h"
#include "verify/verify.h"
#include <cstring>
#include <random>
#include <vector>

using namespace ex369;

int main() {
    const MultiHash* backends[8];
    int n = multihash_all(backends, 8);

    printf("compiled backends usable on this CPU: ");
    for (int i = 0; i < n; i++) printf("%s(x%d) ", backends[i]->name, backends[i]->lanes);
    printf("\n");
    const CpuInfo& c = cpu_info();
    printf("compiled backends: %s\n", compiled_backends().c_str());
#ifdef EX369_HAVE_AVX2
    if (!c.avx2) printf("NOTE: avx2 compiled but NOT usable on this CPU/OS\n");
#endif
#ifdef EX369_HAVE_SSE
    if (!c.ssse3) printf("NOTE: sse compiled but NOT usable on this CPU\n");
#endif

    std::mt19937_64 rng(20260909);
    for (int b = 0; b < n; b++) {
        const MultiHash& mh = *backends[b];
        t::section(std::string("backend ") + mh.name);

        // Real compressed pubkeys, not random bytes: exercises the actual inputs.
        for (int trial = 0; trial < 64; trial++) {
            std::vector<uint8_t> in((size_t)mh.lanes * 33);
            std::vector<uint8_t> got((size_t)mh.lanes * 20), want((size_t)mh.lanes * 20);
            for (int L = 0; L < mh.lanes; L++) {
                U256 k;
                k.d[0] = rng(); k.d[1] = rng() & 0x3FFFFFFFFFFFULL;
                PointA P;
                ec_mul_gen(P, k);
                serialize_compressed(P, &in[(size_t)L * 33]);
                hash160_pubkey33(&in[(size_t)L * 33], &want[(size_t)L * 20]);
            }
            mh.hash160(in.data(), got.data());
            bool same = memcmp(got.data(), want.data(), (size_t)mh.lanes * 20) == 0;
            if (!same) {
                for (int L = 0; L < mh.lanes; L++)
                    printf("    lane %d: got %s want %s\n", L,
                           to_hex(&got[(size_t)L * 20], 20).c_str(),
                           to_hex(&want[(size_t)L * 20], 20).c_str());
            }
            t::check(same, std::string(mh.name) + " matches scalar (trial " +
                           std::to_string(trial) + ")");
        }

        // Edge inputs: all-zero and all-0xFF payloads.
        for (int pat = 0; pat < 2; pat++) {
            std::vector<uint8_t> in((size_t)mh.lanes * 33, pat ? 0xFF : 0x00);
            for (int L = 0; L < mh.lanes; L++) in[(size_t)L * 33] = 0x02;
            std::vector<uint8_t> got((size_t)mh.lanes * 20), want((size_t)mh.lanes * 20);
            for (int L = 0; L < mh.lanes; L++)
                hash160_pubkey33(&in[(size_t)L * 33], &want[(size_t)L * 20]);
            mh.hash160(in.data(), got.data());
            t::check(memcmp(got.data(), want.data(), (size_t)mh.lanes * 20) == 0,
                     std::string(mh.name) + " edge pattern " + std::to_string(pat));
        }

        // Lanes must be independent: distinct inputs, distinct outputs.
        {
            std::vector<uint8_t> in((size_t)mh.lanes * 33, 0);
            for (int L = 0; L < mh.lanes; L++) {
                in[(size_t)L * 33] = 0x02;
                in[(size_t)L * 33 + 32] = (uint8_t)L;
            }
            std::vector<uint8_t> got((size_t)mh.lanes * 20), want((size_t)mh.lanes * 20);
            for (int L = 0; L < mh.lanes; L++)
                hash160_pubkey33(&in[(size_t)L * 33], &want[(size_t)L * 20]);
            mh.hash160(in.data(), got.data());
            t::check(memcmp(got.data(), want.data(), (size_t)mh.lanes * 20) == 0,
                     std::string(mh.name) + " lanes are independent");
        }
    }
    return t::finish("test_hash_simd");
}
