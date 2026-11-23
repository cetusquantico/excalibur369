// Bounded synthetic byte-buffer diagnostics. No scalars, targets or search.
#include "hash/hash_multi.h"
#include "hash/hash160.h"
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace ex369;

int main(int argc, char** argv) {
    uint64_t hashes = 4194304;
    if (argc != 1) {
        if (argc != 3 || std::string(argv[1]) != "--hashes") {
            fprintf(stderr, "usage: hash_diagnostics [--hashes N]\n"); return 2;
        }
        const char* end = argv[2] + std::strlen(argv[2]);
        auto parsed = std::from_chars(argv[2], end, hashes);
        if (parsed.ec != std::errc() || parsed.ptr != end || hashes < 8 || hashes > 67108864 || hashes % 8) {
            fprintf(stderr, "hash count must be a multiple of 8 in 8..67108864\n"); return 2;
        }
    }
    const MultiHash* backends[8];
    int count = multihash_all(backends, 8);
    printf("{\"schema\":1,\"workload\":\"synthetic-33-byte-hash160-v1\",\"samples\":[");
    for (int b = 0; b < count; ++b) {
        const auto& mh = *backends[b];
        std::vector<uint8_t> input(size_t(mh.lanes) * 33), output(size_t(mh.lanes) * 20);
        for (size_t i = 0; i < input.size(); ++i) input[i] = uint8_t(i * 13 + 17);
        volatile uint8_t sink = 0;
        auto start = std::chrono::steady_clock::now();
        for (uint64_t group = 0; group < hashes / mh.lanes; ++group) {
            input[0] = uint8_t(group);
            mh.hash160(input.data(), output.data());
            sink ^= output[0];
        }
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        for (int lane = 0; lane < mh.lanes; ++lane) {
            uint8_t reference[20];
            hash160(input.data() + size_t(lane) * 33, 33, reference);
            if (std::memcmp(reference, output.data() + size_t(lane) * 20, 20)) {
                fprintf(stderr, "hash diagnostic mismatch: %s\n", mh.name); return 1;
            }
        }
        printf("%s{\"backend\":\"%s\",\"hashes\":%llu,\"elapsed_seconds\":%.9f,\"checksum\":%u}",
               b ? "," : "", mh.name, static_cast<unsigned long long>(hashes), elapsed, unsigned(sink));
    }
    printf("]}\n");
    return 0;
}
