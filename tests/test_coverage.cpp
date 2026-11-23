// The coverage contract: every configuration must visit each candidate of its
// keyspace EXACTLY ONCE - no duplicates, no omissions - for every combination
// of traversal mode, direction, fixed-byte rotation and round-robin setting.
#include "testlib.h"
#include "engine.h"
#include "verify/verify.h"
#include <algorithm>
#include <map>
#include <set>

using namespace ex369;

// Small, fully enumerable range: [0x1000, 0x1FFF] = 4096 keys.
static SearchConfig base_cfg() {
    SearchConfig c;
    c.puzzle = 0;
    U256::from_hex("1000", c.range_lo);
    U256::from_hex("1fff", c.range_hi);
    c.has_target = true;              // dummy target; never matched
    c.segment_size = 64;
    c.threads = 1;
    c.batch = 8;
    return c;
}

struct Collected {
    std::vector<std::string> order;
    std::multiset<std::string> all;
    std::set<std::string> uniq;
    bool ok = true;
    std::string err;
};

static Collected collect(SearchConfig c) {
    Collected r;
    Engine e(c);
    if (!e.prepare(r.err)) { r.ok = false; return r; }
    e.enumerate_all([&](const U256& k) {
        std::string h = k.hex();
        r.order.push_back(h);
        r.all.insert(h);
        r.uniq.insert(h);
    });
    return r;
}

static std::set<std::string> full_range_set(uint64_t lo, uint64_t hi) {
    std::set<std::string> s;
    for (uint64_t v = lo; v <= hi; v++) s.insert(U256(v).hex());
    return s;
}

static void expect_exact(const Collected& r, const std::set<std::string>& want,
                         const std::string& label) {
    if (!r.ok) { t::check(false, label + ": prepare failed: " + r.err); return; }
    t::eq_u64(r.all.size(), r.uniq.size(), label + ": no duplicates");
    t::eq_u64(r.uniq.size(), want.size(), label + ": candidate count");
    t::check(r.uniq == want, label + ": exact set match (nothing missing, nothing extra)");
}

int main() {
    const std::set<std::string> FULL = full_range_set(0x1000, 0x1FFF);

    t::section("plain traversal covers the range exactly once");
    for (Order m : {Order::Sequential, Order::Reverse, Order::Random}) {
        for (Direction d : {Direction::Forward, Direction::Backward}) {
            SearchConfig c = base_cfg();
            c.global_order = m;
            c.local_dir = d;
            c.seed = 4242;
            std::string label = std::string("mode=") + order_name(m) + " dir=" + direction_name(d);
            expect_exact(collect(c), FULL, label);
        }
    }

    t::section("segment ordering changes visit order but not the set");
    {
        SearchConfig a = base_cfg(); a.global_order = Order::Sequential;
        SearchConfig b = base_cfg(); b.global_order = Order::Random; b.seed = 1;
        SearchConfig d = base_cfg(); d.global_order = Order::Random; d.seed = 2;
        Collected ra = collect(a), rb = collect(b), rd = collect(d);
        t::check(ra.uniq == rb.uniq && rb.uniq == rd.uniq, "same set across orderings");
        t::check(ra.order != rb.order, "random order differs from sequential");
        t::check(rb.order != rd.order, "different seeds give different orders");
        SearchConfig b2 = base_cfg(); b2.global_order = Order::Random; b2.seed = 1;
        t::check(collect(b2).order == rb.order, "same seed reproduces the exact order");
    }

    t::section("fixed bytes: automatic rotation covers the range exactly once");
    // Byte 30 straddles the range prefix here: only its low 4 bits are free, so
    // the rotation must produce 16 patterns of 256 keys, not 256 of 256.
    for (Order rot : {Order::Sequential, Order::Reverse, Order::Random}) {
        SearchConfig c = base_cfg();
        c.fixed.enabled = true;
        c.fixed.count = 1;
        c.fixed.positions_auto = true;
        c.fixed_rotation = rot;
        c.seed = 77;
        Collected r = collect(c);
        std::string label = std::string("fixed auto rotation=") + order_name(rot);
        expect_exact(r, FULL, label);
    }

    t::section("fixed bytes: pattern space respects the range prefix");
    {
        SearchConfig c = base_cfg();
        c.fixed.enabled = true;
        c.fixed.count = 1;
        c.fixed.positions_auto = true;
        Engine e(c);
        std::string err;
        t::check(e.prepare(err), "prepare ok: " + err);
        t::eq_u64(e.patterns().count(), 16, "16 patterns (4 free bits in byte 30, not 8)");
        t::eq_u64((unsigned long long)e.keyspace().free_bits, 8, "8 free bits remain per pattern");
        t::eq_str(mask_class_name(e.keyspace().klass), "CONTIGUOUS_FAST",
                  "low contiguous free bits -> fast path");
    }

    t::section("fixed bytes: manual values select exactly one sub-cube");
    {
        SearchConfig c = base_cfg();
        c.fixed.enabled = true;
        c.fixed.positions_auto = false;
        c.fixed.positions = {0};          // active-relative: first active byte = 30
        c.fixed.values = {0x1A};
        Collected r = collect(c);
        expect_exact(r, full_range_set(0x1A00, 0x1AFF), "manual value 1A");
    }

    t::section("fixed bytes: a manual value outside the range is rejected");
    {
        SearchConfig c = base_cfg();
        c.fixed.enabled = true;
        c.fixed.positions_auto = false;
        c.fixed.positions = {0};
        c.fixed.values = {0xAA};          // byte 30 must be 0x10..0x1F here
        Engine e(c);
        std::string err;
        t::check(!e.prepare(err), "rejected");
        t::check(err.find("outside the range") != std::string::npos,
                 "error explains the constraint: " + err);
    }

    t::section("round-robin covers the range exactly once and interleaves patterns");
    for (Order rot : {Order::Sequential, Order::Reverse, Order::Random}) {
        for (uidx slice : {(uidx)64, (uidx)128, (uidx)1000}) {
            SearchConfig c = base_cfg();
            c.fixed.enabled = true;
            c.fixed.count = 1;
            c.fixed.positions_auto = true;
            c.fixed_rotation = rot;
            c.round_robin = true;
            c.slice_keys = slice;
            c.seed = 5;
            std::string label = std::string("round-robin rot=") + order_name(rot) +
                                " slice=" + uidx_to_dec(slice);
            expect_exact(collect(c), FULL, label);
        }
    }

    t::section("round-robin actually switches pattern before exhausting it");
    {
        SearchConfig c = base_cfg();
        c.fixed.enabled = true;
        c.fixed.count = 1;
        c.fixed.positions_auto = true;
        c.round_robin = true;
        c.slice_keys = 64;                 // one segment, pattern has 256 keys
        c.segment_size = 64;
        Collected r = collect(c);
        // The high byte identifies the pattern; it must change before 256 keys.
        std::string first = r.order.at(0).substr(60, 2);
        size_t switch_at = 0;
        for (size_t i = 0; i < r.order.size(); i++)
            if (r.order[i].substr(60, 2) != first) { switch_at = i; break; }
        t::check(switch_at > 0 && switch_at <= 64,
                 "pattern switched after the slice (at " + std::to_string(switch_at) + ")");

        SearchConfig c2 = c;
        c2.round_robin = false;
        Collected r2 = collect(c2);
        std::string f2 = r2.order.at(0).substr(60, 2);
        size_t switch2 = 0;
        for (size_t i = 0; i < r2.order.size(); i++)
            if (r2.order[i].substr(60, 2) != f2) { switch2 = i; break; }
        t::eq_u64(switch2, 256, "without round-robin the pattern is exhausted first");
    }

    t::section("two fixed bytes, both rotations, still exact");
    {
        // Wider range so two fixed bytes leave room: [0x100000, 0x1FFFFF].
        SearchConfig c = base_cfg();
        U256::from_hex("100000", c.range_lo);
        U256::from_hex("1fffff", c.range_hi);
        c.fixed.enabled = true;
        c.fixed.count = 2;
        c.fixed.positions_auto = true;
        c.fixed_rotation = Order::Random;
        c.global_order = Order::Random;
        c.seed = 31337;
        c.segment_size = 32;
        Collected r = collect(c);
        expect_exact(r, full_range_set(0x100000, 0x1FFFFF), "2 fixed bytes, random everything");
    }

    t::section("STRIDED classification when the low bytes are pinned");
    {
        SearchConfig c = base_cfg();
        U256::from_hex("100000", c.range_lo);
        U256::from_hex("1fffff", c.range_hi);
        c.fixed.enabled = true;
        c.fixed.positions_auto = false;
        c.fixed.positions = {2};          // active bytes are 29,30,31 -> pin 31 (lowest)
        c.fixed.values = {0x5C};
        c.segment_size = 4096;
        Engine e(c);
        std::string err;
        t::check(e.prepare(err), "prepare: " + err);
        t::eq_str(mask_class_name(e.keyspace().klass), "STRIDED", "pinned low byte -> STRIDED");
        Collected r = collect(c);
        std::set<std::string> want;
        for (uint64_t v = 0x100000; v <= 0x1FFFFF; v++)
            if ((v & 0xFF) == 0x5C) want.insert(U256(v).hex());
        expect_exact(r, want, "STRIDED enumeration");
    }

    t::section("GENERIC classification with a hole in the mask");
    {
        SearchConfig c = base_cfg();
        U256::from_hex("100000", c.range_lo);
        U256::from_hex("1fffff", c.range_hi);
        c.fixed.enabled = true;
        c.fixed.positions_auto = false;
        c.fixed.positions = {1};          // pin the MIDDLE active byte -> two runs
        c.fixed.values = {0xC3};
        c.segment_size = 4096;
        Engine e(c);
        std::string err;
        t::check(e.prepare(err), "prepare: " + err);
        t::eq_str(mask_class_name(e.keyspace().klass), "GENERIC", "hole in mask -> GENERIC");
        Collected r = collect(c);
        std::set<std::string> want;
        for (uint64_t v = 0x100000; v <= 0x1FFFFF; v++)
            if (((v >> 8) & 0xFF) == 0xC3) want.insert(U256(v).hex());
        expect_exact(r, want, "GENERIC enumeration");
    }

    t::section("non-aligned range: edges are clipped, not spilled");
    {
        // Every other test uses an exact sub-cube (as puzzle ranges are), which
        // never exercises the clipping path. This range is deliberately ragged.
        for (Order m : {Order::Sequential, Order::Reverse, Order::Random}) {
            for (Direction d : {Direction::Forward, Direction::Backward}) {
                SearchConfig c = base_cfg();
                U256::from_hex("1234", c.range_lo);
                U256::from_hex("1a5c", c.range_hi);
                c.global_order = m;
                c.local_dir = d;
                c.seed = 8;
                c.segment_size = 32;
                Engine e(c);
                std::string err;
                t::check(e.prepare(err), "prepare: " + err);
                t::check(!e.keyspace().range_is_aligned, "range reported as non-aligned");
                std::string label = std::string("ragged range mode=") + order_name(m) +
                                    " dir=" + direction_name(d);
                expect_exact(collect(c), full_range_set(0x1234, 0x1a5c), label);
            }
        }
    }

    t::section("non-aligned range plus fixed bytes");
    {
        SearchConfig c = base_cfg();
        U256::from_hex("1234", c.range_lo);
        U256::from_hex("1a5c", c.range_hi);
        c.fixed.enabled = true;
        c.fixed.count = 1;
        c.fixed.positions_auto = true;
        c.fixed_rotation = Order::Random;
        c.seed = 12;
        c.segment_size = 32;
        expect_exact(collect(c), full_range_set(0x1234, 0x1a5c), "ragged + fixed rotation");
    }

    return t::finish("test_coverage");
}
