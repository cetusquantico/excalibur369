// EXCALIBUR369 - checkpoint / resume.
//
// The authoritative resume state is the scheduler ticket watermark plus the list
// of tickets completed out of order.  Everything else recorded here is either
// configuration that must match on resume, or human-readable derived state.
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../config.h"
#include "../scheduler/scheduler.h"

namespace ex369 {

struct Checkpoint {
    int         version = 2;
    std::string tool = "EXCALIBUR369";

    // --- configuration fingerprint (must match to resume) ---
    int         puzzle = 0;
    std::string range_lo, range_hi;
    std::string target_hash160;
    std::string global_order;
    std::string local_direction;
    uint64_t    seed = 0;
    std::string fixed_mask;        // 64 hex chars
    std::string fixed_value;       // 64 hex chars of the FIRST pattern
    bool        fixed_enabled = false;
    std::string fixed_rotation;
    std::vector<int> fixed_positions;
    bool        round_robin = false;
    std::string slice_keys;
    std::string segment_size;
    std::string config_hash;       // guards against resuming into a different run

    // --- progress ---
    uint64_t              watermark = 0;
    std::vector<uint64_t> done_above;
    uint64_t              current_pattern = 0;
    std::string           current_segment;
    std::string           local_offset;
    std::string           total_candidates;
    double                elapsed_seconds = 0;

    bool save(const std::string& path, std::string& err) const;
    bool load(const std::string& path, std::string& err);
private:
    std::string payload() const;
};

// Stable fingerprint of the parts of the config that define the search space.
std::string config_fingerprint(const SearchConfig& c, const uint8_t mask[32]);

} // namespace ex369
