// End-to-end: the real hot loop (incremental EC + batch inversion + one-block
// hashes) must find a planted key under every traversal configuration, and the
// independent verifier must confirm it.
#include "testlib.h"
#include "engine.h"
#include "hash/hash160.h"
#include "verify/verify.h"

using namespace ex369;

static void h160_of_key(const U256& k, uint8_t out[20]) {
    PointA P;
    ec_mul_gen(P, k);
    uint8_t c[33];
    serialize_compressed(P, c);
    hash160_pubkey33(c, out);
}

static bool find_key(SearchConfig c, const U256& target_key, std::string& found_hex) {
    h160_of_key(target_key, c.target);
    c.has_target = true;
    Engine e(c);
    std::string err;
    if (!e.prepare(err)) { found_hex = "prepare failed: " + err; return false; }
    e.run();
    if (!e.found()) return false;
    found_hex = e.hits().front().verified.priv_hex;
    return true;
}

int main() {
    SearchConfig base;
    U256::from_hex("1000", base.range_lo);
    U256::from_hex("1fff", base.range_hi);
    base.segment_size = 64;
    base.batch = 8;
    base.threads = 2;
    base.quiet = true;

    t::section("hot loop finds a planted key in every traversal mode");
    for (uint64_t kv : {0x1000ULL, 0x1001ULL, 0x1234ULL, 0x1ffeULL, 0x1fffULL}) {
        for (Order m : {Order::Sequential, Order::Reverse, Order::Random}) {
            for (Direction d : {Direction::Forward, Direction::Backward}) {
                SearchConfig c = base;
                c.global_order = m;
                c.local_dir = d;
                c.seed = 9;
                std::string got;
                bool ok = find_key(c, U256(kv), got);
                char lbl[160];
                snprintf(lbl, sizeof(lbl), "key=0x%llx mode=%s dir=%s",
                         (unsigned long long)kv, order_name(m), direction_name(d));
                t::check(ok, std::string("found ") + lbl + " (" + got + ")");
                if (ok) t::eq_str(got, U256(kv).hex(), std::string("correct key for ") + lbl);
            }
        }
    }

    t::section("hot loop finds a planted key with fixed bytes + round-robin");
    for (uint64_t kv : {0x1000ULL, 0x1a5cULL, 0x1fffULL}) {
        for (bool rr : {false, true}) {
            for (Order rot : {Order::Sequential, Order::Random}) {
                SearchConfig c = base;
                c.fixed.enabled = true;
                c.fixed.count = 1;
                c.fixed.positions_auto = true;
                c.fixed_rotation = rot;
                c.round_robin = rr;
                c.slice_keys = 64;
                c.seed = 11;
                std::string got;
                bool ok = find_key(c, U256(kv), got);
                char lbl[160];
                snprintf(lbl, sizeof(lbl), "key=0x%llx rr=%d rot=%s",
                         (unsigned long long)kv, (int)rr, order_name(rot));
                t::check(ok, std::string("found ") + lbl + " (" + got + ")");
                if (ok) t::eq_str(got, U256(kv).hex(), std::string("correct key for ") + lbl);
            }
        }
    }

    t::section("hot loop finds a planted key on a STRIDED mask");
    {
        SearchConfig c = base;
        U256::from_hex("100000", c.range_lo);
        U256::from_hex("1fffff", c.range_hi);
        c.fixed.enabled = true;
        c.fixed.positions_auto = false;
        c.fixed.positions = {2};
        c.fixed.values = {0x5C};
        c.segment_size = 1024;
        std::string got;
        bool ok = find_key(c, U256(0x18AB5C), got);
        t::check(ok, "found key on STRIDED mask (" + got + ")");
        if (ok) t::eq_str(got, U256(0x18AB5C).hex(), "correct STRIDED key");
    }

    t::section("hot loop finds a planted key on a GENERIC mask");
    {
        SearchConfig c = base;
        U256::from_hex("100000", c.range_lo);
        U256::from_hex("1fffff", c.range_hi);
        c.fixed.enabled = true;
        c.fixed.positions_auto = false;
        c.fixed.positions = {1};
        c.fixed.values = {0xC3};
        c.segment_size = 256;
        std::string got;
        bool ok = find_key(c, U256(0x1FC37A), got);
        t::check(ok, "found key on GENERIC mask (" + got + ")");
        if (ok) t::eq_str(got, U256(0x1FC37A).hex(), "correct GENERIC key");
    }

    t::section("a key outside the configured sub-cube is NOT found");
    {
        SearchConfig c = base;
        c.fixed.enabled = true;
        c.fixed.positions_auto = false;
        c.fixed.positions = {0};
        c.fixed.values = {0x1A};       // only 0x1A00..0x1AFF is searched
        std::string got;
        t::check(!find_key(c, U256(0x1B00), got), "0x1B00 correctly not found");
        t::check(find_key(c, U256(0x1A7F), got), "0x1A7F inside the sub-cube is found");
    }

    t::section("hot loop on a non-aligned range (clipping path)");
    {
        // Keys just inside each edge, and one in the middle.
        for (uint64_t kv : {0x1234ULL, 0x1500ULL, 0x1a5cULL}) {
            for (Order m : {Order::Sequential, Order::Random}) {
                SearchConfig c = base;
                U256::from_hex("1234", c.range_lo);
                U256::from_hex("1a5c", c.range_hi);
                c.global_order = m;
                c.segment_size = 32;
                c.seed = 4;
                std::string got;
                bool ok = find_key(c, U256(kv), got);
                char lbl[128];
                snprintf(lbl, sizeof(lbl), "ragged key=0x%llx mode=%s",
                         (unsigned long long)kv, order_name(m));
                t::check(ok && got == U256(kv).hex(), std::string("found ") + lbl);
            }
        }
        // Keys just OUTSIDE the ragged edges must never be produced.
        for (uint64_t kv : {0x1233ULL, 0x1a5dULL}) {
            SearchConfig c = base;
            U256::from_hex("1234", c.range_lo);
            U256::from_hex("1a5c", c.range_hi);
            c.segment_size = 32;
            std::string got;
            char lbl[128];
            snprintf(lbl, sizeof(lbl), "0x%llx outside the range is not scanned",
                     (unsigned long long)kv);
            t::check(!find_key(c, U256(kv), got), lbl);
        }
    }

    t::section("independent verifier gates every reported hit");
    {
        uint8_t h[20];
        h160_of_key(U256(0x1234), h);
        VerifyResult ok = verify_candidate(U256(0x1234), h);
        t::check(ok.ok, "verifier confirms a real match");
        t::eq_str(ok.priv_hex, U256(0x1234).hex(), "verifier reports the scalar");
        t::check(!ok.address.empty() && ok.address[0] == '1', "verifier produces a P2PKH address");
        VerifyResult bad = verify_candidate(U256(0x1235), h);
        t::check(!bad.ok, "verifier rejects a wrong scalar");
        VerifyResult zero = verify_candidate(U256(0), h);
        t::check(!zero.ok, "verifier rejects the zero scalar");
    }

    t::section("multi-threaded runs agree with single-threaded ones");
    {
        for (int th : {1, 2, 4}) {
            SearchConfig c = base;
            c.threads = th;
            c.global_order = Order::Random;
            c.seed = 3;
            std::string got;
            bool ok = find_key(c, U256(0x1CDE), got);
            t::check(ok && got == U256(0x1CDE).hex(),
                     "threads=" + std::to_string(th) + " finds the key");
        }
    }
    return t::finish("test_engine");
}
