// Per-stage microbenchmark: where does a candidate's time actually go?
#include <cstdio>
#include <vector>
#include <chrono>
#include "ec/group.h"
#include "hash/hash160.h"
#include "hash/hash_multi.h"
#include "cpu/cpu.h"
#include <cstring>

using namespace ex369;
using Clock = std::chrono::steady_clock;
static double since(Clock::time_point t) {
    return std::chrono::duration<double>(Clock::now() - t).count();
}

int main(int argc, char** argv) {
    int N = argc > 1 ? atoi(argv[1]) : 400000;
    int B = 256;

    // --- field ops -----------------------------------------------------------
    // THROUGHPUT, not latency: 8 independent chains, because the EC loop issues
    // independent multiplies across lanes. A single dependent chain measures
    // latency instead and understates real throughput by ~2x.
    {
        const int NC = 8;
        Fe a[NC], b[NC];
        for (int i = 0; i < NC; i++) {
            a[i] = Fe(0x123456789abcdefULL + (uint64_t)i * 7919);
            b[i] = Fe(0xfedcba987654321ULL - (uint64_t)i * 104729);
        }
        int iters = N * 4;
        auto t0 = Clock::now();
        for (int i = 0; i < iters; i++)
            for (int c = 0; c < NC; c++) fe_mul(a[c], a[c], b[c]);
        double d = since(t0);
        printf("fe_mul  (thru): %8.2f M/s   (%5.1f ns/op)\n",
               (double)iters * NC / d / 1e6, 1e9 * d / ((double)iters * NC));

        t0 = Clock::now();
        for (int i = 0; i < iters; i++)
            for (int c = 0; c < NC; c++) fe_sqr(a[c], a[c]);
        d = since(t0);
        printf("fe_sqr  (thru): %8.2f M/s   (%5.1f ns/op)\n",
               (double)iters * NC / d / 1e6, 1e9 * d / ((double)iters * NC));

        t0 = Clock::now();
        for (int i = 0; i < iters; i++)
            for (int c = 0; c < NC; c++) fe_add(a[c], a[c], b[c]);
        d = since(t0);
        printf("fe_add  (thru): %8.2f M/s   (%5.1f ns/op)\n",
               (double)iters * NC / d / 1e6, 1e9 * d / ((double)iters * NC));

        t0 = Clock::now();
        for (int i = 0; i < iters; i++)
            for (int c = 0; c < NC; c++) fe_sub(a[c], a[c], b[c]);
        d = since(t0);
        printf("fe_sub  (thru): %8.2f M/s   (%5.1f ns/op)\n",
               (double)iters * NC / d / 1e6, 1e9 * d / ((double)iters * NC));

#ifdef EX369_FIELD_4X64
        // Only the saturated build can split fe_mul into product + fold; the
        // 5x52 version interleaves them by design.
        uint64_t ww[NC][8];
        t0 = Clock::now();
        for (int i = 0; i < iters; i++)
            for (int c = 0; c < NC; c++) fe_mul512_only(ww[c], a[c], b[c]);
        d = since(t0);
        printf("  512-bit product only : %5.1f ns\n", 1e9 * d / ((double)iters * NC));

        t0 = Clock::now();
        for (int i = 0; i < iters; i++)
            for (int c = 0; c < NC; c++) fe_reduce512(a[c], ww[c]);
        d = since(t0);
        printf("  reduce512 only       : %5.1f ns\n", 1e9 * d / ((double)iters * NC));
#endif

        Fe r;
        t0 = Clock::now();
        for (int i = 0; i < N / 4; i++) { fe_inv(r, a[0]); fe_add(a[0], a[0], r); }
        d = since(t0);
        printf("fe_inv        : %8.2f K/s   (%5.0f ns/op)\n",
               (N / 4.0) / d / 1e3, 1e9 * d / (N / 4.0));
        uint8_t keepb[32]; fe_to_be32(a[0], keepb);
        volatile uint8_t keep = keepb[0]; (void)keep;
    }

    // --- batched EC step ---
    {
        std::vector<PointA> P(B);
        std::vector<Fe> dx(B), inv(B), sc(B);
        for (int j = 0; j < B; j++) ec_mul_gen(P[j], U256((uint64_t)(1000000 + j * 7919)));
        const PointA& G = generator();
        auto t0 = Clock::now();
        int steps = N / B * 4;
        for (int s = 0; s < steps; s++) {
            for (int j = 0; j < B; j++) fe_sub(dx[j], G.x, P[j].x);
            batch_inverse(inv.data(), dx.data(), B);
            for (int j = 0; j < B; j++) {
                Fe lam, t, nx;
                fe_sub(t, G.y, P[j].y);
                fe_mul(lam, t, inv[j]);
                fe_sqr(nx, lam);
                fe_sub(nx, nx, P[j].x); fe_sub(nx, nx, G.x);
                fe_sub(t, P[j].x, nx); fe_mul(t, lam, t);
                fe_sub(P[j].y, t, P[j].y); P[j].x = nx;
            }
        }
        double d = since(t0);
        printf("EC batch step : %8.2f M pts/s  (batch=%d)\n", (double)steps * B / d / 1e6, B);
    }

    // --- hashing (no modulo in the loop; sink the result so nothing is elided) ---
    {
        const int M = 256;                       // power of two -> mask, not divide
        std::vector<uint8_t> pub(33 * M);
        for (size_t i = 0; i < pub.size(); i++) pub[i] = (uint8_t)(i * 37 + 11);
        uint8_t sh[32], h[20];
        volatile uint32_t sink = 0;
        int iters = N * 4;

        auto t0 = Clock::now();
        for (int i = 0; i < iters; i++) {
            sha256_33_ref(&pub[(i & (M - 1)) * 33], sh);
            sink ^= sh[0];
        }
        double d = since(t0);
        printf("sha256_33 REF : %8.2f M/s   (%6.0f ns/op)\n",
               iters / d / 1e6, 1e9 * d / iters);

        t0 = Clock::now();
        for (int i = 0; i < iters; i++) {
            sha256_33(&pub[(i & (M - 1)) * 33], sh);
            sink ^= sh[0];
        }
        d = since(t0);
        printf("sha256_33     : %8.2f M/s   (%6.0f ns/op)\n",
               iters / d / 1e6, 1e9 * d / iters);

        t0 = Clock::now();
        for (int i = 0; i < iters; i++) {
            sh[0] = (uint8_t)i;
            ripemd160_32(sh, h);
            sink ^= h[0];
        }
        d = since(t0);
        printf("ripemd160_32  : %8.2f M/s   (%6.0f ns/op)\n",
               iters / d / 1e6, 1e9 * d / iters);

        t0 = Clock::now();
        for (int i = 0; i < iters; i++) {
            sha256_33(&pub[(i & (M - 1)) * 33], sh);
            ripemd160_32(sh, h);
            sink ^= h[0];
        }
        d = since(t0);
        printf("hash160 total : %8.2f M/s   (%6.0f ns/op)\n",
               iters / d / 1e6, 1e9 * d / iters);
        (void)sink;
    }
    // --- multi-buffer backends ---
    {
        printf("\n-- multi-buffer HASH160 --\n");
        const MultiHash* bk[8];
        int nb = multihash_all(bk, 8);
        double base = 0;
        for (int i = 0; i < nb; i++) {
            const MultiHash& mh = *bk[i];
            std::vector<uint8_t> in((size_t)mh.lanes * 33), out((size_t)mh.lanes * 20);
            for (size_t z = 0; z < in.size(); z++) in[z] = (uint8_t)(z * 31 + 7);
            int groups = N * 4 / mh.lanes;
            auto t0 = Clock::now();
            for (int g = 0; g < groups; g++) {
                in[0] = (uint8_t)g;
                mh.hash160(in.data(), out.data());
            }
            double d = since(t0);
            double rate = (double)groups * mh.lanes / d / 1e6;
            if (i == 0) base = rate;
            printf("%-10s x%-2d : %8.2f M hash160/s   (%5.0f ns/hash)  %5.2fx vs scalar\n",
                   mh.name, mh.lanes, rate, 1e9 * d / (groups * mh.lanes), rate / base);
        }
    }
    // --- engine-shaped loop, stage by stage (B=512, one core) ---------------
    {
        printf("\n-- engine loop, cumulative stages (B=512) --\n");
        const int EB = 512;
        std::vector<PointA> P(EB);
        std::vector<Fe> dx(EB), inv(EB), sc2(EB);
        std::vector<uint8_t> pub((size_t)EB * 33), h160((size_t)EB * 20);
        const PointA& G = generator();
        const MultiHash& mh = multihash_for(select_backend(""));
        uint64_t bloom[1024] = {0};
        volatile uint64_t sink = 0;
        int steps = 400;

        auto reset = [&] {
            for (int j = 0; j < EB; j++) ec_mul_gen(P[j], U256((uint64_t)(1000000 + j * 7919)));
        };
        auto ecstep = [&] {
            for (int j = 0; j < EB; j++) fe_sub(dx[j], G.x, P[j].x);
            batch_inverse(inv.data(), dx.data(), EB);
            for (int j = 0; j < EB; j++) {
                Fe lam, t, nx;
                fe_sub(t, G.y, P[j].y);
                fe_mul(lam, t, inv[j]);
                fe_sqr(nx, lam);
                fe_sub(nx, nx, P[j].x); fe_sub(nx, nx, G.x);
                fe_sub(t, P[j].x, nx); fe_mul(t, lam, t);
                fe_sub(P[j].y, t, P[j].y); P[j].x = nx;
            }
        };
        auto serialize = [&] {
            for (int j = 0; j < EB; j++) {
                uint8_t* p = &pub[(size_t)j * 33];
                p[0] = P[j].y.is_odd() ? 0x03 : 0x02;
                fe_to_be32(P[j].x, p + 1);
            }
        };
        auto dohash = [&] {
            int j = 0;
            for (; j + mh.lanes <= EB; j += mh.lanes)
                mh.hash160(&pub[(size_t)j * 33], &h160[(size_t)j * 20]);
        };
        auto docheck = [&] {
            for (int j = 0; j < EB; j++) {
                uint64_t k;
                memcpy(&k, &h160[(size_t)j * 20], 8);
                sink += (bloom[(k >> 3) & 1023] >> (k & 63)) & 1ULL;
            }
        };

        struct { const char* name; int mask; } stages[] = {
            {"EC step only",              1},
            {"+ serialize pubkey",        3},
            {"+ hash160 (SIMD)",          7},
            {"+ target compare (FULL)",  15},
        };
        double prev = 0;
        for (auto& st : stages) {
            reset();
            auto t0 = Clock::now();
            for (int s = 0; s < steps; s++) {
                if (st.mask & 1) ecstep();
                if (st.mask & 2) serialize();
                if (st.mask & 4) dohash();
                if (st.mask & 8) docheck();
            }
            double d = since(t0);
            double ns = 1e9 * d / ((double)steps * EB);
            printf("%-26s %8.3f M/s  %6.1f ns/cand  (+%5.1f ns)\n",
                   st.name, (double)steps * EB / d / 1e6, ns, ns - prev);
            prev = ns;
        }
        (void)sink;
    }
    return 0;
}
