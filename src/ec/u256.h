// EXCALIBUR369 - 256-bit unsigned integer (little-endian 4x64 limbs).
// Used for private scalars and range arithmetic. Correctness-first.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <array>

namespace ex369 {

struct U256 {
    // d[0] = least significant 64 bits.
    uint64_t d[4];

    U256() { d[0] = d[1] = d[2] = d[3] = 0; }
    explicit U256(uint64_t v) { d[0] = v; d[1] = d[2] = d[3] = 0; }

    static U256 zero() { return U256(); }
    static U256 one() { return U256(1); }

    bool is_zero() const { return (d[0] | d[1] | d[2] | d[3]) == 0; }

    // Big-endian 32-byte serialization (Bitcoin convention).
    void to_be32(uint8_t out[32]) const {
        for (int i = 0; i < 4; i++) {
            uint64_t w = d[3 - i];
            out[i * 8 + 0] = (uint8_t)(w >> 56);
            out[i * 8 + 1] = (uint8_t)(w >> 48);
            out[i * 8 + 2] = (uint8_t)(w >> 40);
            out[i * 8 + 3] = (uint8_t)(w >> 32);
            out[i * 8 + 4] = (uint8_t)(w >> 24);
            out[i * 8 + 5] = (uint8_t)(w >> 16);
            out[i * 8 + 6] = (uint8_t)(w >> 8);
            out[i * 8 + 7] = (uint8_t)(w);
        }
    }
    static U256 from_be32(const uint8_t in[32]) {
        U256 r;
        for (int i = 0; i < 4; i++) {
            uint64_t w = 0;
            for (int j = 0; j < 8; j++) w = (w << 8) | in[i * 8 + j];
            r.d[3 - i] = w;
        }
        return r;
    }

    // byte_index uses BIG-ENDIAN indexing: 0 = most significant byte, 31 = least significant.
    uint8_t byte_be(int idx) const {
        int shift = (31 - idx) * 8;          // bit offset from LSB
        return (uint8_t)(d[shift >> 6] >> (shift & 63));
    }
    void set_byte_be(int idx, uint8_t v) {
        int shift = (31 - idx) * 8;
        int limb = shift >> 6, off = shift & 63;
        d[limb] = (d[limb] & ~(0xFFULL << off)) | ((uint64_t)v << off);
    }

    int cmp(const U256& o) const {
        for (int i = 3; i >= 0; i--) {
            if (d[i] < o.d[i]) return -1;
            if (d[i] > o.d[i]) return 1;
        }
        return 0;
    }
    bool operator<(const U256& o) const { return cmp(o) < 0; }
    bool operator<=(const U256& o) const { return cmp(o) <= 0; }
    bool operator>(const U256& o) const { return cmp(o) > 0; }
    bool operator>=(const U256& o) const { return cmp(o) >= 0; }
    bool operator==(const U256& o) const { return cmp(o) == 0; }
    bool operator!=(const U256& o) const { return cmp(o) != 0; }

    // Returns carry out.
    static uint64_t add(U256& r, const U256& a, const U256& b) {
        unsigned long long c = 0;
        for (int i = 0; i < 4; i++) {
            unsigned long long t = (unsigned long long)a.d[i] + b.d[i];
            unsigned long long carry1 = (t < a.d[i]) ? 1 : 0;
            unsigned long long t2 = t + c;
            unsigned long long carry2 = (t2 < t) ? 1 : 0;
            r.d[i] = t2;
            c = carry1 + carry2;
        }
        return c;
    }
    // Returns borrow out.
    static uint64_t sub(U256& r, const U256& a, const U256& b) {
        unsigned long long bor = 0;
        for (int i = 0; i < 4; i++) {
            unsigned long long t = a.d[i] - b.d[i];
            unsigned long long b1 = (a.d[i] < b.d[i]) ? 1 : 0;
            unsigned long long t2 = t - bor;
            unsigned long long b2 = (t < bor) ? 1 : 0;
            r.d[i] = t2;
            bor = b1 + b2;
        }
        return bor;
    }
    U256 operator+(const U256& o) const { U256 r; add(r, *this, o); return r; }
    U256 operator-(const U256& o) const { U256 r; sub(r, *this, o); return r; }

    U256& operator+=(uint64_t v) {
        unsigned long long c = v;
        for (int i = 0; i < 4 && c; i++) {
            unsigned long long t = d[i] + c;
            c = (t < d[i]) ? 1 : 0;
            d[i] = t;
        }
        return *this;
    }

    U256 shl(unsigned n) const {
        U256 r;
        if (n >= 256) return r;
        unsigned w = n >> 6, b = n & 63;
        for (int i = 3; i >= 0; i--) {
            uint64_t v = 0;
            int src = i - (int)w;
            if (src >= 0) {
                v = d[src] << b;
                if (b && src - 1 >= 0) v |= d[src - 1] >> (64 - b);
            }
            r.d[i] = v;
        }
        return r;
    }

    int bit(unsigned i) const {
        if (i >= 256) return 0;
        return (int)((d[i >> 6] >> (i & 63)) & 1);
    }
    // Index of the highest set bit, or -1 for zero.
    int bit_length() const {
        for (int i = 3; i >= 0; i--)
            if (d[i]) {
                int n = 63;
                while (n >= 0 && !((d[i] >> n) & 1)) n--;
                return i * 64 + n + 1;
            }
        return 0;
    }

    std::string hex() const {
        static const char* H = "0123456789abcdef";
        uint8_t b[32];
        to_be32(b);
        std::string s(64, '0');
        for (int i = 0; i < 32; i++) { s[i * 2] = H[b[i] >> 4]; s[i * 2 + 1] = H[b[i] & 15]; }
        return s;
    }
    // Accepts optional 0x prefix, any length up to 64 hex digits.
    static bool from_hex(const std::string& in, U256& out) {
        std::string s = in;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
        if (s.empty() || s.size() > 64) return false;
        out = U256();
        for (char c : s) {
            int v;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else return false;
            // out = out*16 + v
            uint64_t carry = (uint64_t)v;
            for (int i = 0; i < 4; i++) {
                uint64_t hi = out.d[i] >> 60;
                out.d[i] = (out.d[i] << 4) | carry;
                carry = hi;
            }
            if (carry) return false; // overflow past 256 bits
        }
        return true;
    }
};

} // namespace ex369
