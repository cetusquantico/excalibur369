#include "verify.h"
#include "../ec/group.h"
#include "../hash/sha256.h"
#include "../hash/ripemd160.h"
#include <cstring>
#include <vector>
#include <algorithm>

namespace ex369 {

static const char* B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

std::string to_hex(const uint8_t* p, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; i++) { s[i*2] = H[p[i] >> 4]; s[i*2+1] = H[p[i] & 15]; }
    return s;
}

bool from_hex(const std::string& s, uint8_t* out, size_t n) {
    if (s.size() != n * 2) return false;
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < n; i++) {
        int a = v(s[i*2]), b = v(s[i*2+1]);
        if (a < 0 || b < 0) return false;
        out[i] = (uint8_t)((a << 4) | b);
    }
    return true;
}

std::string base58check_encode(const uint8_t* payload, size_t n, uint8_t version) {
    std::vector<uint8_t> buf;
    buf.push_back(version);
    buf.insert(buf.end(), payload, payload + n);
    uint8_t h1[32], h2[32];
    sha256(buf.data(), buf.size(), h1);
    sha256(h1, 32, h2);
    buf.insert(buf.end(), h2, h2 + 4);

    size_t zeros = 0;
    while (zeros < buf.size() && buf[zeros] == 0) zeros++;

    std::vector<uint8_t> digits;
    for (size_t i = zeros; i < buf.size(); i++) {
        int carry = buf[i];
        for (size_t j = 0; j < digits.size(); j++) {
            int t = digits[j] * 256 + carry;
            digits[j] = (uint8_t)(t % 58);
            carry = t / 58;
        }
        while (carry) { digits.push_back((uint8_t)(carry % 58)); carry /= 58; }
    }
    std::string out(zeros, '1');
    for (size_t i = digits.size(); i-- > 0;) out.push_back(B58[digits[i]]);
    if (digits.empty() && zeros == 0) out = "";
    return out;
}

bool base58check_decode(const std::string& s, uint8_t* out, size_t& n, uint8_t& version) {
    std::vector<uint8_t> bytes;
    size_t zeros = 0;
    while (zeros < s.size() && s[zeros] == '1') zeros++;
    for (size_t i = zeros; i < s.size(); i++) {
        const char* p = strchr(B58, s[i]);
        if (!p || s[i] == 0) return false;
        int carry = (int)(p - B58);
        for (size_t j = 0; j < bytes.size(); j++) {
            int t = bytes[j] * 58 + carry;
            bytes[j] = (uint8_t)(t & 0xFF);
            carry = t >> 8;
        }
        while (carry) { bytes.push_back((uint8_t)(carry & 0xFF)); carry >>= 8; }
    }
    std::vector<uint8_t> full(zeros, 0);
    for (size_t i = bytes.size(); i-- > 0;) full.push_back(bytes[i]);
    if (full.size() < 5) return false;
    uint8_t h1[32], h2[32];
    sha256(full.data(), full.size() - 4, h1);
    sha256(h1, 32, h2);
    if (memcmp(h2, full.data() + full.size() - 4, 4) != 0) return false;
    version = full[0];
    size_t payload = full.size() - 5;
    if (payload > n) return false;
    memcpy(out, full.data() + 1, payload);
    n = payload;
    return true;
}

bool address_to_hash160(const std::string& addr, uint8_t out[20]) {
    if (addr.empty() || addr.size() > 35) return false;
    uint8_t buf[64];
    size_t n = sizeof(buf);
    uint8_t ver;
    if (!base58check_decode(addr, buf, n, ver)) return false;
    if (n != 20) return false;
    if (ver != 0x00) return false;   // P2PKH mainnet only
    memcpy(out, buf, 20);
    return true;
}

// --- independent scalar multiplication: affine double-and-add ---------------
static void independent_mul_gen(PointA& out, const U256& k) {
    PointA acc; acc.inf = true;
    PointA cur = generator();
    for (int i = 0; i < 256; i++) {
        if (k.bit(i)) {
            PointA t;
            point_a_add(t, acc, cur);
            acc = t;
        }
        PointA d;
        point_a_add(d, cur, cur);
        cur = d;
    }
    out = acc;
}

static std::string wif_compressed(const U256& k) {
    uint8_t p[33];
    k.to_be32(p);
    p[32] = 0x01;
    return base58check_encode(p, 33, 0x80);
}

VerifyResult verify_candidate(const U256& priv, const uint8_t* target20) {
    VerifyResult r;
    r.priv = priv;
    r.priv_hex = priv.hex();

    if (priv.is_zero()) { r.error = "private scalar is zero"; return r; }
    U256 n = U256::from_be32((const uint8_t*)"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                                             "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFE"
                                             "\xBA\xAE\xDC\xE6\xAF\x48\xA0\x3B"
                                             "\xBF\xD2\x5E\x8C\xD0\x36\x41\x41");
    if (priv >= n) { r.error = "private scalar is >= group order"; return r; }

    PointA P;
    independent_mul_gen(P, priv);
    if (P.inf) { r.error = "point at infinity"; return r; }

    uint8_t pub[33];
    pub[0] = P.y.is_odd() ? 0x03 : 0x02;
    fe_to_be32(P.x, pub + 1);
    r.pub_compressed_hex = to_hex(pub, 33);

    // Generic streaming hashes (not the one-block hot-loop specializations).
    uint8_t s[32], h[20];
    sha256(pub, 33, s);
    ripemd160(s, 32, h);
    r.hash160_hex = to_hex(h, 20);
    r.address = base58check_encode(h, 20, 0x00);
    r.wif = wif_compressed(priv);

    if (target20 && memcmp(h, target20, 20) != 0) { r.error = "hash160 mismatch"; return r; }
    r.ok = true;
    return r;
}

} // namespace ex369
