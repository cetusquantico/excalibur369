#include "mask.h"
#include <algorithm>
#include <sstream>
#include <set>

namespace ex369 {

std::string uidx_to_dec(uidx v) {
    if (v == 0) return "0";
    char buf[48];
    int i = 47;
    buf[i--] = 0;
    while (v) { buf[i--] = (char)('0' + (int)(v % 10)); v /= 10; }
    return std::string(buf + i + 1);
}

bool uidx_from_dec(const std::string& s, uidx& out) {
    if (s.empty()) return false;
    uidx v = 0;
    // hex form
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (size_t i = 2; i < s.size(); i++) {
            char c = s[i];
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return false;
            if (v > (~(uidx)0 - (uidx)d) / 16) return false;
            v = v * 16 + (uidx)d;
        }
        out = v;
        return true;
    }
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        uidx digit = (uidx)(c - '0');
        if (v > (~(uidx)0 - digit) / 10) return false;
        v = v * 10 + digit;
    }
    out = v;
    return true;
}

const char* mask_class_name(MaskClass c) {
    switch (c) {
        case MaskClass::CONTIGUOUS_FAST: return "CONTIGUOUS_FAST";
        case MaskClass::STRIDED:         return "STRIDED";
        default:                          return "GENERIC";
    }
}

int active_first_byte(const U256& lo, const U256& hi) {
    for (int i = 0; i < 32; i++)
        if (lo.byte_be(i) != hi.byte_be(i)) return i;
    return 32;  // lo == hi: single key
}

// Number of leading bits (from MSB) where lo and hi agree.
static int common_prefix_bits(const U256& lo, const U256& hi) {
    for (int b = 255; b >= 0; b--)
        if (lo.bit(b) != hi.bit(b)) return 255 - b;
    return 256;
}

int range_free_bits(const U256& lo, const U256& hi) {
    return 256 - common_prefix_bits(lo, hi);
}

bool KeySpace::finalize(std::string& err) {
    if (range_lo > range_hi) { err = "range_lo > range_hi"; return false; }

    int pref = common_prefix_bits(range_lo, range_hi);
    int range_free = 256 - pref;   // bits below the common prefix

    // The range is "aligned" (an exact sub-cube) when the free part of lo is all
    // zeros and the free part of hi is all ones. Bitcoin puzzle ranges are.
    range_is_aligned = true;
    for (int b = 0; b < range_free; b++) {
        if (range_lo.bit(b) != 0 || range_hi.bit(b) != 1) { range_is_aligned = false; break; }
    }

    // Base scalar: the range prefix, plus every mask-pinned bit taken from value.
    base = range_lo;
    for (int b = 0; b < range_free; b++) base.d[b >> 6] &= ~(1ULL << (b & 63));
    // Pin masked bits, but ONLY inside the range-free region: bits above it
    // belong to the range prefix and must never be overwritten by the mask.
    for (int b = 0; b < range_free; b++) {
        int byte_idx = 31 - (b >> 3);
        int bit_in_byte = b & 7;
        if (!((mask[byte_idx] >> bit_in_byte) & 1)) continue;
        if ((value[byte_idx] >> bit_in_byte) & 1) base.d[b >> 6] |= (1ULL << (b & 63));
    }

    // Free bits: below the range prefix AND not pinned by the mask.
    runs.clear();
    free_bits = 0;
    int run_start = -1;
    for (int b = 0; b < range_free; b++) {
        int byte_idx = 31 - (b >> 3);            // big-endian byte holding bit b
        int bit_in_byte = b & 7;
        bool pinned = (mask[byte_idx] >> bit_in_byte) & 1;
        if (!pinned) {
            if (run_start < 0) run_start = b;
            free_bits++;
        } else {
            if (run_start >= 0) { runs.push_back({run_start, b - run_start}); run_start = -1; }
        }
    }
    if (run_start >= 0) runs.push_back({run_start, range_free - run_start});

    if (free_bits > 127) { err = "more than 127 free bits is not supported"; return false; }

    // Classify.
    if (runs.empty()) {
        klass = MaskClass::CONTIGUOUS_FAST;
    } else if (runs.size() == 1) {
        klass = (runs[0].start == 0) ? MaskClass::CONTIGUOUS_FAST : MaskClass::STRIDED;
    } else {
        klass = MaskClass::GENERIC;
    }
    inner_bits = runs.empty() ? 0 : runs[0].len;
    inner_stride = U256(1).shl(runs.empty() ? 0u : (unsigned)runs[0].start);
    return true;
}

U256 KeySpace::scalar_at(uidx index) const {
    U256 k = base;
    int consumed = 0;
    for (const BitRun& r : runs) {
        // Take r.len bits out of index starting at `consumed`, place them at r.start.
        for (int j = 0; j < r.len; j++) {
            int src = consumed + j;
            uidx bit = (index >> src) & 1;
            if (bit) {
                int dst = r.start + j;
                k.d[dst >> 6] |= (1ULL << (dst & 63));
            }
        }
        consumed += r.len;
    }
    return k;
}

std::string KeySpace::describe() const {
    std::ostringstream o;
    o << "keyspace:\n";
    o << "  range   : " << range_lo.hex() << "\n";
    o << "          .. " << range_hi.hex() << "\n";
    o << "  aligned : " << (range_is_aligned ? "yes" : "no (edges will be clipped)") << "\n";
    o << "  base    : " << base.hex() << "\n";
    o << "  class   : " << mask_class_name(klass) << "\n";
    o << "  free    : " << free_bits << " bits  (2^" << free_bits << " indices)\n";
    o << "  runs    :";
    for (const BitRun& r : runs) o << " [" << r.start << "+" << r.len << "]";
    o << "\n  inner   : " << inner_bits << " bits, stride 0x" << inner_stride.hex() << "\n";
    return o.str();
}

bool resolve_positions(const FixedConfig& cfg, const U256& lo, const U256& hi,
                       std::vector<int>& out_abs, std::string& err) {
    out_abs.clear();
    if (!cfg.enabled) return true;

    int first_active = active_first_byte(lo, hi);
    if (first_active >= 32) { err = "range covers a single key; nothing to fix"; return false; }
    int nactive = 32 - first_active;

    if (!cfg.positions_auto) {
        for (int p : cfg.positions) {
            if (p < 0 || p > 31) { err = "fixed position must be in 0..31"; return false; }
            int abs = cfg.absolute_positions ? p : (first_active + p);
            if (abs < 0 || abs > 31) {
                err = "fixed position " + std::to_string(p) + " is out of the 32-byte scalar";
                return false;
            }
            if (abs < first_active) {
                err = "fixed position " + std::to_string(p) +
                      " (absolute byte " + std::to_string(abs) +
                      ") lies in the range prefix and is already pinned";
                return false;
            }
            out_abs.push_back(abs);
        }
    } else {
        // auto: take the highest active bytes (they cut the space fastest and keep
        // the low bytes contiguous, which preserves the incremental fast path).
        int n = cfg.count;
        if (n <= 0) { err = "--fixed-count must be >= 1 when positions are auto"; return false; }
        if (n > nactive - 1) {
            err = "--fixed-count " + std::to_string(n) + " leaves no free byte (active bytes: " +
                  std::to_string(nactive) + ")";
            return false;
        }
        for (int i = 0; i < n; i++) out_abs.push_back(first_active + i);
    }

    std::set<int> uniq(out_abs.begin(), out_abs.end());
    if (uniq.size() != out_abs.size()) { err = "duplicate fixed byte positions"; return false; }
    // Keep the user's order: positions[i] is paired with values[i].
    return true;
}

bool resolve_slots(const FixedConfig& cfg, const U256& lo, const U256& hi,
                   std::vector<FixedSlot>& out, std::string& err) {
    out.clear();
    std::vector<int> abs;
    if (!resolve_positions(cfg, lo, hi, abs, err)) return false;
    if (!cfg.enabled) return true;

    const int rfree = range_free_bits(lo, hi);
    for (int b : abs) {
        FixedSlot s;
        s.byte = b;
        // Byte b (big-endian) holds scalar bits [lowbit, lowbit+8).
        int lowbit = (31 - b) * 8;
        int free_in_byte = rfree - lowbit;          // how many of its bits are free
        if (free_in_byte > 8) free_in_byte = 8;
        if (free_in_byte <= 0) {
            err = "fixed byte " + std::to_string(b) + " lies entirely in the range prefix";
            return false;
        }
        s.free_bits = free_in_byte;
        // The byte's high bits come from the range itself.
        s.prefix_value = (uint8_t)(lo.byte_be(b) & ~s.free_mask());
        out.push_back(s);
    }

    // Leave at least one free bit for the search itself.
    int pinned = 0;
    for (const FixedSlot& s : out) pinned += s.free_bits;
    if (pinned >= rfree) {
        err = "the fixed bytes pin all " + std::to_string(rfree) +
              " free bits of the range; nothing left to search";
        return false;
    }
    return true;
}

} // namespace ex369
