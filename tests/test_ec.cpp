// EC correctness: known vectors, field laws, and forward/reverse traversal.
#include "testlib.h"
#include "ec/group.h"
#include "hash/hash160.h"
#include "verify/verify.h"
#include <cstring>
#include <random>
#include <algorithm>

using namespace ex369;

static std::string pub_of(const U256& k) {
    PointA P;
    ec_mul_gen(P, k);
    uint8_t c[33];
    serialize_compressed(P, c);
    return to_hex(c, 33);
}
static std::string h160_of(const U256& k) {
    PointA P;
    ec_mul_gen(P, k);
    uint8_t c[33], h[20];
    serialize_compressed(P, c);
    hash160_pubkey33(c, h);
    return to_hex(h, 20);
}

int main() {
    t::section("field arithmetic laws (randomized)");
    {
        std::mt19937_64 rng(20260909);
        auto rand_fe = [&] {
            uint8_t b[32];
            for (int j = 0; j < 32; j++) b[j] = (uint8_t)(rng() >> 24);
            return fe_from_be32_mod(b);      // representation-agnostic
        };
        for (int i = 0; i < 3000; i++) {
            Fe a = rand_fe(), b = rand_fe(), c = rand_fe();

            Fe x, y, z, w;
            fe_add(x, a, b); fe_add(y, b, a);
            t::check(fe_eq(x, y), "add commutes");
            fe_mul(x, a, b); fe_mul(y, b, a);
            t::check(fe_eq(x, y), "mul commutes");
            fe_mul(x, a, b); fe_mul(x, x, c);
            fe_mul(y, b, c); fe_mul(y, a, y);
            t::check(fe_eq(x, y), "mul associates");
            fe_add(x, b, c); fe_mul(x, a, x);
            fe_mul(y, a, b); fe_mul(z, a, c); fe_add(y, y, z);
            t::check(fe_eq(x, y), "mul distributes");
            fe_sub(x, a, b); fe_add(x, x, b);
            t::check(fe_eq(x, a), "sub inverts add");
            fe_sqr(x, a); fe_mul(y, a, a);
            t::check(fe_eq(x, y), "sqr == mul self");
            if (!a.is_zero()) {
                fe_inv(x, a); fe_mul(y, a, x);
                t::check(fe_eq(y, Fe(1)), "a * a^-1 == 1");
            }
            fe_neg(x, a); fe_add(x, x, a);
            t::check(x.is_zero(), "a + (-a) == 0");
            (void)w;
        }
    }

    t::section("private key -> public key (known vectors)");
    {
        U256 k;
        t::eq_str(pub_of(U256(1)),
            "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798", "1*G");
        t::eq_str(pub_of(U256(2)),
            "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5", "2*G");
        t::eq_str(pub_of(U256(3)),
            "02f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9", "3*G");
        U256::from_hex("2b191ec1f0b1c9a8a2f3d0d9d1c69fd7db77a9f5ba1e13e2c2b1a51a0d0c0b0a", k);
        t::check(pub_of(k).size() == 66, "arbitrary key serializes");

        // n-1 gives the negation of G (same x, odd/even flipped).
        U256::from_hex("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364140", k);
        t::eq_str(pub_of(k).substr(2), pub_of(U256(1)).substr(2), "(n-1)*G shares x with G");
        t::check(pub_of(k).substr(0,2) != pub_of(U256(1)).substr(0,2), "(n-1)*G flips parity");
    }

    t::section("public key -> HASH160 -> address (known vectors)");
    {
        t::eq_str(h160_of(U256(1)), "751e76e8199196d454941c45d1b3a323f1433bd6", "hash160(1*G)");
        uint8_t h[20];
        from_hex(h160_of(U256(1)), h, 20);
        t::eq_str(base58check_encode(h, 20, 0), "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH", "address(1*G)");

        // Bitcoin puzzles #1..#5 - solved long ago, keys are public knowledge.
        // Every address here was regenerated from libsecp256k1, not recalled.
        struct { const char* k; const char* addr; } pz[] = {
            {"1",  "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH"},   // #1, key 1
            {"3",  "1CUNEBjYrCn2y1SdiUMohaKUi4wpP326Lb"},   // #2, key 3
            {"7",  "19ZewH8Kk1PDbSNdJ97FP4EiCjTRaZMZQA"},   // #3, key 7
            {"8",  "1EhqbyUMvvs7BfL8goY6qcPbD6YKfPqb7e"},   // #4, key 8
            {"15", "1E6NuFjCi27W5zoXg8TRdcSRq84zJeBW3k"},   // #5, key 0x15 = 21
        };
        for (auto& e : pz) {
            U256 k;
            U256::from_hex(e.k, k);
            uint8_t hh[20];
            from_hex(h160_of(k), hh, 20);
            t::eq_str(base58check_encode(hh, 20, 0), e.addr,
                      std::string("puzzle key ") + e.k + " -> address");
        }
    }

    t::section("forward traversal: k, k+1, k+2 via P += G");
    {
        U256 k;
        U256::from_hex("400000000000000000000000000000000000abcdef0123456789", k);
        PointA P;
        ec_mul_gen(P, k);
        const PointA& G = generator();
        for (int i = 0; i < 64; i++) {
            uint8_t c[33];
            serialize_compressed(P, c);
            U256 ki = k;
            ki += (uint64_t)i;
            t::eq_str(to_hex(c, 33), pub_of(ki),
                      "P+=G step " + std::to_string(i) + " matches (k+" + std::to_string(i) + ")*G");
            PointA n;
            point_a_add(n, P, G);
            P = n;
        }
    }

    t::section("reverse traversal: k, k-1, k-2 via P -= G");
    {
        U256 k;
        U256::from_hex("400000000000000000000000000000000000abcdef0123456789", k);
        PointA P;
        ec_mul_gen(P, k);
        PointA negG;
        point_a_neg(negG, generator());
        for (int i = 0; i < 64; i++) {
            uint8_t c[33];
            serialize_compressed(P, c);
            U256 ki = k - U256((uint64_t)i);
            t::eq_str(to_hex(c, 33), pub_of(ki),
                      "P-=G step " + std::to_string(i) + " matches (k-" + std::to_string(i) + ")*G");
            PointA n;
            point_a_add(n, P, negG);
            P = n;
        }
    }

    t::section("forward and reverse cover the identical key set");
    {
        U256 k0;
        U256::from_hex("7fffffffffffffffffffff", k0);
        std::vector<std::string> fwd, rev;
        {
            PointA P;
            ec_mul_gen(P, k0);
            for (int i = 0; i < 128; i++) {
                uint8_t c[33];
                serialize_compressed(P, c);
                fwd.push_back(to_hex(c, 33));
                PointA n; point_a_add(n, P, generator()); P = n;
            }
        }
        {
            U256 kend = k0 + U256(127);
            PointA P;
            ec_mul_gen(P, kend);
            PointA negG; point_a_neg(negG, generator());
            for (int i = 0; i < 128; i++) {
                uint8_t c[33];
                serialize_compressed(P, c);
                rev.push_back(to_hex(c, 33));
                PointA n; point_a_add(n, P, negG); P = n;
            }
        }
        std::reverse(rev.begin(), rev.end());
        t::check(fwd == rev, "forward set == reversed backward set");
    }

    t::section("jacobian ops are safe when output aliases input");
    {
        // The batched lane chain calls point_j_add_affine(acc, acc, D), and an
        // x-collision routes that into point_j_double with r aliasing p.
        std::mt19937_64 rng(1234);
        for (int i = 0; i < 200; i++) {
            U256 k;
            k.d[0] = rng() | 1; k.d[1] = rng() & 0xFFFF;
            PointA A;
            ec_mul_gen(A, k);
            PointJ J; J.X = A.x; J.Y = A.y; J.Z = Fe(1);

            PointJ aliased = J, separate;
            point_j_double(aliased, aliased);       // r == p
            point_j_double(separate, J);            // r != p
            PointA a1, a2;
            point_j_to_affine(a1, aliased);
            point_j_to_affine(a2, separate);
            t::check(fe_eq(a1.x, a2.x) && fe_eq(a1.y, a2.y), "aliased double == separate double");

            // and it must equal 2k*G
            PointA want;
            ec_mul_gen(want, k + k);
            t::check(fe_eq(a1.x, want.x) && fe_eq(a1.y, want.y), "aliased double == (2k)*G");

            // aliased mixed add, including the self-add that triggers doubling
            PointJ acc = J;
            point_j_add_affine(acc, acc, A);        // P + P -> doubling path
            PointA a3;
            point_j_to_affine(a3, acc);
            t::check(fe_eq(a3.x, want.x) && fe_eq(a3.y, want.y), "aliased self-add == (2k)*G");
        }
    }

    t::section("batch inversion == individual inversion");
    {
        std::mt19937_64 rng(7);
        const int N = 300;
        std::vector<Fe> in(N), out(N), sc(N);
        for (int i = 0; i < N; i++) {
            uint8_t b[32];
            for (int j = 0; j < 32; j++) b[j] = (uint8_t)(rng() >> 24);
            in[i] = fe_from_be32_mod(b);
        }
        in[5] = Fe();   // include a zero to exercise the guard
        batch_inverse(out.data(), in.data(), N);
        for (int i = 0; i < N; i++) {
            if (in[i].is_zero()) { t::check(out[i].is_zero(), "zero stays zero"); continue; }
            Fe one; fe_mul(one, in[i], out[i]);
            t::check(fe_eq(one, Fe(1)), "batch inverse correct at " + std::to_string(i));
        }
    }

    t::section("jacobian add/double agree with affine");
    {
        std::mt19937_64 rng(99);
        for (int i = 0; i < 200; i++) {
            U256 a, b;
            a.d[0] = rng(); a.d[1] = rng() & 0xFFFF;
            b.d[0] = rng(); b.d[1] = rng() & 0xFFFF;
            PointA A, B, sumAff;
            ec_mul_gen(A, a);
            ec_mul_gen(B, b);
            point_a_add(sumAff, A, B);
            PointJ J; J.X = A.x; J.Y = A.y; J.Z = Fe(1);
            PointJ R;
            point_j_add_affine(R, J, B);
            PointA sumJac;
            point_j_to_affine(sumJac, R);
            t::check(fe_eq(sumAff.x, sumJac.x) && fe_eq(sumAff.y, sumJac.y), "jacobian == affine add");
            U256 s = a + b;
            PointA direct;
            ec_mul_gen(direct, s);
            t::check(fe_eq(direct.x, sumAff.x), "(a+b)*G == a*G + b*G");
        }
    }
    return t::finish("test_ec");
}
