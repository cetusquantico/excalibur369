// Deterministic enumeration checks on tiny synthetic spaces, with no key search.
#include "testlib.h"
#include "engine.h"
#include <algorithm>
#include <set>
using namespace ex369;

static SearchConfig config(uint64_t lo, uint64_t hi) {
    SearchConfig c;
    c.range_lo = U256(lo); c.range_hi = U256(hi);
    c.has_target = true; c.threads = 1; c.quiet = true;
    c.global_order = Order::Reverse; c.local_dir = Direction::Backward;
    return c;
}
static std::vector<std::string> enumerate(const SearchConfig& c) {
    Engine e(c); std::string err;
    if (!e.prepare(err)) { t::check(false, err); return {}; }
    std::vector<std::string> values;
    e.enumerate_all([&](const U256& k) { values.push_back(k.hex()); });
    return values;
}
int main() {
    t::section("reference enumeration is descending (not a trace of the batched engine)");
    for (auto range : {std::pair<uint64_t,uint64_t>{1,1}, {5,10}, {0xff,0x104},
                       {0x1000,0x1fff}, {0x1234,0x1a5c}}) {
        std::vector<std::string> expected;
        for (uint64_t k = range.second;; --k) {
            expected.push_back(U256(k).hex());
            if (k == range.first) break;
        }
        for (uidx segment : {uidx(1),uidx(8),uidx(64),uidx(4096)}) {
            auto c = config(range.first, range.second); c.segment_size = segment;
            t::check(enumerate(c) == expected, "inclusive endpoints, boundaries and descending order");
        }
    }
    t::section("reverse segments with explicit forward direction is a separate ordering");
    auto c = config(0x1000, 0x100f); c.segment_size = 8; c.local_dir = Direction::Forward;
    std::vector<std::string> expected;
    for (uint64_t k : {0x1008,0x1009,0x100a,0x100b,0x100c,0x100d,0x100e,0x100f,
                      0x1000,0x1001,0x1002,0x1003,0x1004,0x1005,0x1006,0x1007})
        expected.push_back(U256(k).hex());
    t::check(enumerate(c) == expected, "global order and local direction are independent");

    t::section("fixed pattern rotations preserve the set without promising global descending order");
    expected.clear();
    for (uint64_t k = 0x1234; k <= 0x1456; ++k) expected.push_back(U256(k).hex());
    for (Order rotation : {Order::Sequential,Order::Reverse,Order::Random})
        for (bool rr : {false,true}) {
            c = config(0x1234,0x1456); c.segment_size = 16;
            c.fixed.enabled = true; c.fixed.count = 1;
            c.fixed_rotation = rotation; c.round_robin = rr; c.slice_keys = 32;
            auto got = enumerate(c);
            std::sort(got.begin(), got.end());
            t::check(got == expected, "fixed reverse exact set, including clipped edges");
        }
    return t::finish("test_reverse");
}
