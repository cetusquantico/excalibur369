#include "testlib.h"
#include "engine.h"
#include "checkpoint/checkpoint.h"
#include <filesystem>
#include <unistd.h>

using namespace ex369;

static SearchConfig config(uint64_t lo=0x1000, uint64_t hi=0x1fff) {
    SearchConfig c;
    c.range_lo=U256(lo); c.range_hi=U256(hi);
    c.has_target=true; // Dummy digest: no real target or wallet.
    c.threads=1; c.batch=7; c.quiet=true;
    return c;
}

int main() {
    t::section("diagnose empty range padding without counting it as checked work");
    {
        auto c=config(0x4a,0x7a);
        c.global_order=Order::Reverse; c.local_dir=Direction::Backward;
        c.segment_size=1; c.batch=1;
        Engine plan(c); std::string err;
        t::check(plan.prepare(err),"prepare tiny non-aligned model");
        WorkUnit unit;
        // The known planning limitation is reproduced without cryptography:
        // the first five units are above the requested inclusive upper bound.
        for (int i=0;i<5;i++) {
            t::check(plan.scheduler().claim(unit),"padding unit exists");
            t::check(plan.keyspace().scalar_at(unit.index_begin)>c.range_hi,"initial unit outside requested interval");
        }
        t::check(plan.scheduler().claim(unit),"first in-range unit exists");
        t::check(plan.keyspace().scalar_at(unit.index_begin)==c.range_hi,"eventual first unit reaches upper bound");
        Engine engine(c);
        t::check(engine.prepare(err),"prepare bounded dummy run");
        auto report=engine.run();
        t::eq_u64(report.candidates,49,"49 actual candidate checks");
        t::eq_u64(report.segments,64,"64 scheduler completions, including padding");
        t::eq_u64(report.outside_range_segments,15,"15 entirely out-of-range units reported separately");
        t::check(report.format("tiny").find("completed with work: 49")!=std::string::npos,"report distinguishes useful completions");
        BenchReport idle; idle.segments=5; idle.outside_range_segments=5;
        t::check(idle.format("idle").find("no candidates reported")!=std::string::npos,"zero work cannot masquerade as checked coverage");
        t::check(candidate_progress_note(0,0).empty(),"ordinary startup has no discard diagnostic");
        t::check(candidate_progress_note(49,15).empty(),"checked work is not labeled as zero");
        LocalCounters local; Counters global;
        local.segments=5; local.outside_range_segments=5;
        local.flush(global); local.flush(global);
        t::eq_u64(global.outside_range_segments.load(),5,"discard counters are not counted again by a later flush");
    }
    t::section("the real engine clips both boundaries of a segment");
    for (auto bounds : {std::pair<uint64_t,uint64_t>{5,10}, {0x1234,0x1a5c}, {0x1000,0x1000}, {1,1}})
        for (Order order : {Order::Sequential,Order::Reverse,Order::Random})
            for (Direction dir : {Direction::Forward,Direction::Backward})
                for (uidx segment : {(uidx)8,(uidx)4096}) {
                    auto c=config(bounds.first,bounds.second);
                    c.segment_size=segment; c.global_order=order; c.local_dir=dir;
                    Engine e(c); std::string err;
                    bool ok=e.prepare(err); t::check(ok,"prepare clipping: "+err);
                    if (!ok) continue;
                    t::eq_u64(e.run().candidates,bounds.second-bounds.first+1,"every valid candidate processed");
                    t::check(e.space_size()==(double)(bounds.second-bounds.first+1),"exact progress denominator");
                }

    t::section("manual bytes retain their position/value association");
    auto c=config(0x10000000,0x1fffffff);
    c.fixed.enabled=true; c.fixed.positions_auto=false;
    c.fixed.positions={2,1}; c.fixed.values={0x11,0x22};
    Engine e(c); std::string err;
    t::check(e.prepare(err),"prepare unsorted positions: "+err);
    t::eq_str(e.keyspace().base.hex(),U256(0x10221100).hex(),"position 2=11, position 1=22");

    t::section("integer and index bounds");
    uidx parsed;
    t::check(!uidx_from_dec("340282366920938463463374607431768211456",parsed),"reject 2^128");
    t::check(uidx_from_dec("340282366920938463463374607431768211455",parsed),"accept largest u128");
    auto wide=config(); wide.puzzle=128; wide.segment_size=(uidx)1<<64;
    Engine w(wide);
    t::check(w.prepare(err),"127 free bits can be represented");
    t::check(w.keyspace().total_indices()==((uidx)1<<127),"2^127 indices");
    t::check(w.segments_per_pattern()==((uidx)1<<63),"no overflow in segment count");
    wide.segment_size=1;
    Engine too_many(wide);
    t::check(!too_many.prepare(err),"reject ticket overflow");
    auto zero=config(0,7); Engine z(zero);
    t::check(!z.prepare(err),"reject zero scalar in range");

    t::section("checkpoint fingerprints distinguish manual values and rotation order");
    char tmp[]="/tmp/ex369-regression-XXXXXX";
    char* dir=mkdtemp(tmp);
    if (!dir) return 1;
    std::string path=std::string(dir)+"/checkpoint.json";
    c=config(); c.fixed.enabled=true; c.fixed.positions_auto=false;
    c.fixed.positions={0}; c.fixed.values={0x1a}; c.segment_size=64; c.checkpoint_path=path;
    Engine initial(c); t::check(initial.prepare(err),"prepare first checkpoint");
    t::eq_u64(initial.run().candidates,256,"initial subrange fully examined");
    c.resume=true; c.fixed.values={0x1b};
    Engine changed(c); t::check(!changed.prepare(err),"different manual bytes refused");
    c.fixed.values.clear();
    Engine rotated(c); t::check(!rotated.prepare(err),"manual-to-auto change refused");

    t::section("restore validates tickets and preserves pending completion records");
    Scheduler scheduler;
    t::check(scheduler.init(1,8,1,8,false,8,Order::Sequential,0,err),"init scheduler");
    Scheduler::Progress progress; progress.watermark=9;
    t::check(!scheduler.restore(progress,err),"reject watermark after end");
    progress.watermark=1; progress.done_above={2,2};
    t::check(!scheduler.restore(progress,err),"reject duplicate completed ticket");
    progress.done_above={8};
    t::check(!scheduler.restore(progress,err),"reject completed ticket after end");
    progress.done_above={2,4};
    t::check(scheduler.restore(progress,err),"restore valid progress");
    t::check(scheduler.snapshot().done_above==progress.done_above,"immediate snapshot preserves completions");
    WorkUnit unit;
    std::vector<uint64_t> remaining;
    while(scheduler.claim(unit)) { remaining.push_back(unit.ticket); scheduler.complete(unit.ticket); }
    t::check(remaining==std::vector<uint64_t>({1,3,5,6,7}),"resume visits only unfinished tickets");
    std::filesystem::remove_all(dir);
    return t::finish("test_regressions");
}
