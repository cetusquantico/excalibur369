// Known-answer tests for the hash layer and address encoding.
#include "testlib.h"
#include "hash/sha256.h"
#include "hash/ripemd160.h"
#include "hash/hash160.h"
#include "verify/verify.h"
#include <cstring>

using namespace ex369;

int main() {
    t::section("SHA-256 known answers");
    {
        uint8_t o[32];
        sha256((const uint8_t*)"", 0, o);
        t::eq_str(to_hex(o, 32),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256(\"\")");
        sha256((const uint8_t*)"abc", 3, o);
        t::eq_str(to_hex(o, 32),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256(\"abc\")");
        const char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        sha256((const uint8_t*)m, strlen(m), o);
        t::eq_str(to_hex(o, 32),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256(56 bytes)");
        // 1000 'a' - exercises multi-block
        std::string big(1000, 'a');
        sha256((const uint8_t*)big.data(), big.size(), o);
        t::eq_str(to_hex(o, 32),
            "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3", "sha256(1000*a)");
    }

    t::section("RIPEMD-160 known answers");
    {
        uint8_t o[20];
        ripemd160((const uint8_t*)"", 0, o);
        t::eq_str(to_hex(o, 20), "9c1185a5c5e9fc54612808977ee8f548b2258d31", "ripemd160(\"\")");
        ripemd160((const uint8_t*)"abc", 3, o);
        t::eq_str(to_hex(o, 20), "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc", "ripemd160(\"abc\")");
        const char* m = "message digest";
        ripemd160((const uint8_t*)m, strlen(m), o);
        t::eq_str(to_hex(o, 20), "5d0689ef49d2fae572b881b123a85ffa21595f36", "ripemd160(msg digest)");
        std::string big(1000, 'a');
        ripemd160((const uint8_t*)big.data(), big.size(), o);
        t::eq_str(to_hex(o, 20), "aa69deee9a8922e92f8105e007f76110f381e9cf", "ripemd160(1000*a)");
    }

    t::section("one-block specializations match the generic paths");
    {
        for (int trial = 0; trial < 512; trial++) {
            uint8_t in[33];
            for (int i = 0; i < 33; i++) in[i] = (uint8_t)(trial * 7 + i * 31);
            uint8_t a[32], b[32];
            sha256(in, 33, a);
            sha256_33(in, b);
            t::check(memcmp(a, b, 32) == 0, "sha256_33 == sha256");
            uint8_t c[20], d[20];
            ripemd160(a, 32, c);
            ripemd160_32(a, d);
            t::check(memcmp(c, d, 20) == 0, "ripemd160_32 == ripemd160");
            uint8_t e[20], f[20];
            hash160(in, 33, e);
            hash160_pubkey33(in, f);
            t::check(memcmp(e, f, 20) == 0, "hash160_pubkey33 == hash160");
        }
    }

    t::section("Base58Check round-trip and known addresses");
    {
        uint8_t h[20];
        from_hex("751e76e8199196d454941c45d1b3a323f1433bd6", h, 20);
        t::eq_str(base58check_encode(h, 20, 0x00), "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH", "addr(1G pubkey)");

        uint8_t back[20];
        t::check(address_to_hash160("1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH", back), "decode addr");
        t::check(memcmp(h, back, 20) == 0, "addr round-trip");

        // Puzzle #1 address.
        t::check(address_to_hash160("1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH", back), "puzzle1 addr");
        // Bad checksum must be rejected.
        t::check(!address_to_hash160("1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMI", back), "reject bad checksum");
        t::check(!address_to_hash160("not_an_address", back), "reject garbage");
    }
    return t::finish("test_hash");
}
