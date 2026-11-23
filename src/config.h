// EXCALIBUR369 - run configuration shared by CLI, scheduler and engine.
#pragma once
#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include "ec/u256.h"
#include "fixed/mask.h"

namespace ex369 {

enum class Order { Sequential, Reverse, Random };
enum class Direction { Forward, Backward };

const char* order_name(Order o);
bool parse_order(const std::string& s, Order& out);
const char* direction_name(Direction d);

struct SearchConfig {
    int   puzzle = 0;                 // 0 => explicit range
    U256  range_lo, range_hi;

    std::string target_address;
    uint8_t     target[20] = {0};     // the single --address/--hash160 target
    bool        has_target = false;
    // Extra targets loaded from --file. All of them, plus `target` when
    // has_target, end up in the engine's TargetSet.
    std::vector<std::array<uint8_t, 20>> extra_targets;
    std::string target_file;

    Order      global_order = Order::Sequential;  // segment ordering
    Direction  local_dir    = Direction::Forward; // inside a segment
    uint64_t   seed         = 0;

    FixedConfig fixed;
    Order       fixed_rotation = Order::Sequential;

    bool  round_robin = false;
    uidx  slice_keys  = ((uidx)1) << 30;

    uidx  segment_size = ((uidx)1) << 22;
    int   threads      = 0;           // 0 => physical cores
    bool  affinity     = false;
    bool  avoid_smt    = true;
    int   batch        = 2048;        // EC lanes per batch inversion (measured optimum on
                                      // SSE/4-core Ivy Bridge: +1.85% vs 512, won 5/5
                                      // paired runs; 3072+ regress, L2 spill)
    std::string simd   = "";          // "" => auto

    std::string checkpoint_path;
    std::string output_path;          // optional confirmed results, private JSONL
    int         checkpoint_interval_s = 30;
    bool        resume = false;

    uidx  limit_keys = 0;             // 0 => unlimited (benchmark / test aid)
    int   status_interval_s = 0;      // 0 => no periodic progress line
    bool  quiet = false;
};

// Bitcoin puzzle N: range [2^(N-1), 2^N - 1].
bool puzzle_range(int n, U256& lo, U256& hi);

} // namespace ex369
