// EXCALIBUR369 - independent verification of a candidate match.
//
// Deliberately does NOT share code with the hot loop:
//   * scalar multiplication is plain affine double-and-add (one fe_inv per add),
//     not the batched Jacobian engine;
//   * hashing uses the generic streaming sha256()/ripemd160(), not the
//     fixed-length one-block specializations;
//   * the result is re-encoded all the way to a Base58Check address.
// A hit is only ever reported after this path confirms it.
#pragma once
#include <string>
#include <cstdint>
#include "../ec/u256.h"

namespace ex369 {

struct VerifyResult {
    bool ok = false;
    U256 priv;
    std::string priv_hex;
    std::string pub_compressed_hex;
    std::string hash160_hex;
    std::string address;
    std::string wif;
    std::string error;
};

// Recompute everything from `priv`. When target20 is non-null the result is
// only ok if hash160 matches it; pass nullptr to validate the derivation alone
// and let the caller test membership in a multi-target set.
VerifyResult verify_candidate(const U256& priv, const uint8_t* target20);

std::string base58check_encode(const uint8_t* payload, size_t n, uint8_t version);
bool base58check_decode(const std::string& s, uint8_t* out, size_t& n, uint8_t& version);
// Decode a P2PKH address into its HASH160. Returns false if malformed.
bool address_to_hash160(const std::string& addr, uint8_t out[20]);

std::string to_hex(const uint8_t* p, size_t n);
bool from_hex(const std::string& s, uint8_t* out, size_t n);

} // namespace ex369
