// Checkpoint / resume must never SKIP a candidate.  Redoing a little work is
// acceptable; losing coverage is not.
#include "testlib.h"
#include "engine.h"
#include "checkpoint/checkpoint.h"
#include "verify/verify.h"
#include <cstdio>
#include <set>
#include <unistd.h>

using namespace ex369;

static SearchConfig base_cfg(const std::string& path) {
    SearchConfig c;
    U256::from_hex("1000", c.range_lo);
    U256::from_hex("1fff", c.range_hi);
    c.has_target = true;
    c.segment_size = 64;
    c.threads = 1;
    c.batch = 8;
    c.checkpoint_path = path;
    c.quiet = true;
    return c;
}

int main() {
    char temp[] = "/tmp/ex369-test-checkpoint-XXXXXX";
    int fd = mkstemp(temp);
    if (fd < 0) return 1;
    close(fd);
    const std::string path = temp;
    remove(path.c_str());

    t::section("checkpoint round-trips every mandated field");
    {
        SearchConfig c = base_cfg(path);
        c.fixed.enabled = true;
        c.fixed.count = 1;
        c.fixed.positions_auto = true;
        c.fixed_rotation = Order::Random;
        c.global_order = Order::Random;
        c.local_dir = Direction::Backward;
        c.round_robin = true;
        c.slice_keys = 128;
        c.seed = 4242;
        Engine e(c);
        std::string err;
        t::check(e.prepare(err), "prepare: " + err);
        t::check(e.write_checkpoint(err), "write: " + err);

        Checkpoint cp;
        t::check(cp.load(path, err), "load: " + err);
        t::eq_str(cp.range_lo, U256(0x1000).hex(), "range_lo");
        t::eq_str(cp.range_hi, U256(0x1fff).hex(), "range_hi");
        t::eq_str(cp.global_order, "random-no-repeat", "traversal mode");
        t::eq_str(cp.local_direction, "reverse(P-=G)", "direction");
        t::eq_u64(cp.seed, 4242, "seed");
        t::check(cp.fixed_enabled, "fixed enabled");
        t::eq_str(cp.fixed_rotation, "random-no-repeat", "rotation mode");
        t::check(cp.fixed_mask.size() == 64, "fixed mask present");
        t::check(cp.fixed_value.size() == 64, "fixed value present");
        t::check(!cp.fixed_positions.empty(), "fixed positions present");
        t::check(cp.round_robin, "round-robin state");
        t::eq_str(cp.slice_keys, "128", "slice keys");
        t::eq_str(cp.segment_size, "64", "segment size");
        t::check(!cp.config_hash.empty(), "config fingerprint");
        t::check(!cp.total_candidates.empty(), "total candidates");
        t::check(!cp.current_segment.empty(), "current segment");
        t::check(!cp.local_offset.empty(), "local offset");
    }

    t::section("resume refuses a checkpoint from a different configuration");
    {
        SearchConfig c = base_cfg(path);
        c.seed = 999;               // different config -> different fingerprint
        c.resume = true;
        Engine e(c);
        std::string err;
        bool ok = e.prepare(err);
        t::check(!ok, "mismatched checkpoint rejected");
        t::check(err.find("different configuration") != std::string::npos, "clear error: " + err);
    }

    t::section("interrupted run + resume loses no candidate");
    {
        remove(path.c_str());
        std::set<std::string> seen;
        SearchConfig c = base_cfg(path);
        c.global_order = Order::Random;
        c.seed = 1234;

        // Phase 1: consume part of the space, then checkpoint.
        {
            Engine e(c);
            std::string err;
            t::check(e.prepare(err), "prepare 1: " + err);
            int n = 0;
            WorkUnit u;
            KeySpace ks = e.keyspace();
            while (n < 20 && e.scheduler().claim(u)) {
                for (uidx i = u.index_begin; i < u.index_end; i++) seen.insert(ks.scalar_at(i).hex());
                e.scheduler().complete(u.ticket);
                n++;
            }
            t::check(e.write_checkpoint(err), "write ckpt: " + err);
            t::eq_u64(seen.size(), 20 * 64, "phase 1 covered 20 segments");
        }

        // Phase 2: resume and finish.
        {
            SearchConfig c2 = c;
            c2.resume = true;
            Engine e(c2);
            std::string err;
            t::check(e.prepare(err), "prepare 2: " + err);
            size_t before = seen.size();
            size_t added = 0, repeats = 0;
            KeySpace ks = e.keyspace();
            WorkUnit u;
            while (e.scheduler().claim(u)) {
                for (uidx i = u.index_begin; i < u.index_end; i++) {
                    std::string h = ks.scalar_at(i).hex();
                    if (seen.insert(h).second) added++; else repeats++;
                }
                e.scheduler().complete(u.ticket);
            }
            t::eq_u64(seen.size(), 4096, "resume completed the whole range");
            t::eq_u64(added, 4096 - before, "resume added exactly the missing keys");
            t::eq_u64(repeats, 0, "resume repeated nothing");
        }
    }

    t::section("an interrupted segment is redone, never skipped");
    {
        remove(path.c_str());
        SearchConfig c = base_cfg(path);
        Engine e(c);
        std::string err;
        t::check(e.prepare(err), "prepare: " + err);
        WorkUnit a, b, cu;
        e.scheduler().claim(a);
        e.scheduler().claim(b);
        e.scheduler().claim(cu);
        // Complete out of order, leaving `a` unfinished (as an interrupted
        // worker would): the watermark must NOT advance past it.
        e.scheduler().complete(b.ticket);
        e.scheduler().complete(cu.ticket);
        Scheduler::Progress p = e.scheduler().snapshot();
        t::eq_u64(p.watermark, a.ticket, "watermark stalls on the unfinished ticket");
        t::eq_u64(p.done_above.size(), 2, "the two finished tickets are recorded");
        t::check(e.write_checkpoint(err), "write: " + err);

        SearchConfig c2 = c;
        c2.resume = true;
        Engine e2(c2);
        t::check(e2.prepare(err), "prepare resume: " + err);
        WorkUnit first;
        t::check(e2.scheduler().claim(first), "claim after resume");
        t::eq_u64(first.ticket, a.ticket, "the interrupted segment is handed out again");
    }

    remove(path.c_str());
    return t::finish("test_checkpoint");
}
