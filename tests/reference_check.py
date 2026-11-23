#!/usr/bin/env python3
"""Cross-check EXCALIBUR369 against an INDEPENDENT reference implementation.

Uses libsecp256k1 (via coincurve) and hashlib - a completely separate codebase -
to validate private key -> pubkey -> HASH160 -> P2PKH address, plus forward
(k, k+1, k+2) and reverse (k, k-1, k-2) traversal.

Usage: reference_check.py <path-to-kh71>
"""
import hashlib
import random
import subprocess
import sys

try:
    import coincurve
except ImportError:
    print("SKIP: coincurve not installed")
    sys.exit(77)

B58 = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'


def b58check(payload: bytes, version: int) -> str:
    d = bytes([version]) + payload
    d += hashlib.sha256(hashlib.sha256(d).digest()).digest()[:4]
    n = int.from_bytes(d, 'big')
    s = ''
    while n:
        n, r = divmod(n, 58)
        s = B58[r] + s
    return '1' * (len(d) - len(d.lstrip(b'\x00'))) + s


def reference(k: int):
    pub = coincurve.PrivateKey(k.to_bytes(32, 'big')).public_key.format(True)
    h160 = hashlib.new('ripemd160', hashlib.sha256(pub).digest()).digest()
    wif = b58check(k.to_bytes(32, 'big') + b'\x01', 0x80)
    return pub.hex(), h160.hex(), b58check(h160, 0x00), wif


def under_test(binary: str, k: int):
    out = subprocess.run([binary, '--verify', '%064x' % k],
                         capture_output=True, text=True, check=True).stdout
    got = {}
    for line in out.splitlines():
        if ':' in line:
            key, val = line.split(':', 1)
            got[key.strip()] = val.strip()
    return got['pubkey'], got['hash160'], got['address'], got['wif']


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else './build/kh71'
    rng = random.Random(20260909)
    fails = 0
    checks = 0

    keys = [1, 2, 3, 7, 8, 21, 0x45, 2**70, 2**71 - 1, 2**160 + 12345]
    # a few random keys in the puzzle-71 range
    keys += [rng.randrange(2**70, 2**71) for _ in range(8)]
    # traversal: k, k+-1, k+-2 around several anchors
    for anchor in [2**70 + 999, 0x1000, rng.randrange(2**70, 2**71)]:
        keys += [anchor - 2, anchor - 1, anchor, anchor + 1, anchor + 2]

    for k in keys:
        exp = reference(k)
        got = under_test(binary, k)
        for label, a, b in zip(('pubkey', 'hash160', 'address', 'wif'), got, exp):
            checks += 1
            if a != b:
                fails += 1
                print(f"FAIL k={k:#x} {label}\n  got      {a}\n  expected {b}")

    print(f"reference_check: {checks - fails} passed, {fails} failed "
          f"({len(keys)} keys vs libsecp256k1)")
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
