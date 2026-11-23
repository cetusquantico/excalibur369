#!/usr/bin/env python3
"""Checks only the already-solved public fixture supplied by the user."""
import hashlib
import argparse
import itertools
import json
from pathlib import Path
import re
import subprocess
import sys
try:
    import coincurve
except ImportError:
    print("SKIP: coincurve not installed")
    sys.exit(77)

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
parser.add_argument('--qemu-cpu', help='optional local CPU emulation for this solved fixture')
options = parser.parse_args()
binary = options.binary.resolve()
runner = ['qemu-x86_64', '-cpu', options.qemu_cpu] if options.qemu_cpu else []
key = int('4b5f8303e9', 16)
address = '122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT8'
pub = coincurve.PrivateKey(key.to_bytes(32, 'big')).public_key.format(True)
digest = hashlib.new('ripemd160', hashlib.sha256(pub).digest()).hexdigest()
rows = []

def check(name, args, expected=True, verify=False, candidates=None):
    p = subprocess.run([*runner, str(binary), *args], capture_output=True, text=True, timeout=15)
    found = '*** FOUND' in p.stdout
    ok = p.returncode == 0 and found == expected
    if verify or found:
        fields = dict((k.strip(), v.strip()) for k, v in re.findall(r'^\s*(private|pubkey|hash160|address|match)\s*:\s*(.*)$', p.stdout, re.M))
        ok = p.returncode == 0 and fields.get('private') == f'{key:064x}' and fields.get('pubkey') == pub.hex() and fields.get('hash160') == digest and fields.get('address') == address
        if not verify: ok = ok and found == expected
    if candidates is not None:
        total = re.search(r'total candidates\s*:\s*(\d+)', p.stdout)
        ok = ok and total is not None and int(total.group(1)) == candidates
    row = {'name': name, 'passed': ok, 'expected_found': expected,
           'found': found, 'returncode': p.returncode, 'args': args}
    if not ok: row.update(stdout=p.stdout, stderr=p.stderr)
    rows.append(row)

check('independent_verify', ['--verify', f'{key:064x}', '--address', address], verify=True)
window = f'{key & ~0xfff:x}:{key | 0xfff:x}'
base = ['--range', window, '--address', address, '--quiet']
for mode, direction, threads, simd in itertools.product(('sequential', 'reverse', 'random'), ('forward', 'reverse'), ('1', '4'), ('scalar', 'sse')):
    args = base + ['--mode', mode, '--direction', direction, '--threads', threads, '--simd', simd, '--seed', '917']
    check(f'plain/{mode}/{direction}/{threads}/{simd}', args)
for mode, direction, rotation, rr in itertools.product(('sequential', 'reverse', 'random'), ('forward', 'reverse'), ('sequential', 'reverse', 'random'), ('on', 'off')):
    args = base + ['--mode', mode, '--direction', direction, '--fixed', 'on', '--fixed-count', '1', '--fixed-rotation', rotation,
                   '--round-robin', rr, '--slice-keys', '64', '--segment-size', '64', '--threads', '2', '--seed', '917']
    check(f'fixed/{mode}/{direction}/{rotation}/{rr}', args)
for batch in ('1', '3', '7', '17', '2048'):
    check('batch/' + batch, base + ['--batch', batch, '--simd', 'scalar', '--threads', '1'])
for direction in ('forward', 'reverse'):
    args = ['--range', f'{key & ~0xffff:x}:{key | 0xffff:x}', '--address', address, '--threads', '1',
            '--fixed', 'on', '--fixed-positions', '1', '--fixed-values', 'e9', '--direction', direction, '--quiet']
    check('strided/' + direction, args)
    args = ['--range', '4b5f800000:4b5f8fffff', '--address', address, '--threads', '1',
            '--fixed', 'on', '--fixed-positions', '1', '--fixed-values', '03', '--direction', direction, '--quiet']
    check('generic/' + direction, args)
for side, interval in [('start', f'{key:x}:{key+31:x}'), ('end', f'{key-31:x}:{key:x}')]:
    check('edge/' + side, ['--range', interval, '--address', address, '--threads', '1', '--segment-size', '8', '--quiet'])
for side, interval in [('before', f'{key-32:x}:{key-1:x}'), ('after', f'{key+1:x}:{key+32:x}')]:
    check('excluded/' + side, ['--range', interval, '--address', address, '--threads', '1', '--segment-size', '8', '--quiet'], expected=False)

# CLI defaults and explicit override, exercised through the actual engine.
# With a single lane the strict order is observable from the known hit position.
for segment in ('1', '8', '64'):
    for offset in (0, 1, 7, 15):
        lo, hi = key-offset, key-offset+15
        args = ['--range', f'{lo:x}:{hi:x}', '--address', address, '--threads', '1',
                '--batch', '1', '--simd', 'scalar', '--segment-size', segment, '--mode', 'reverse', '--quiet']
        check(f'reverse-default/{segment}/{offset}', args, candidates=hi-key+1)
        check(f'reverse-explicit/{segment}/{offset}', args + ['--direction', 'reverse'], candidates=hi-key+1)
# All values fit in one segment, so forward explicitly overrides reverse's default.
check('reverse-forward-override', ['--range', f'{key-1:x}:{key+6:x}', '--address', address,
      '--threads', '1', '--batch', '1', '--segment-size', '4096', '--simd', 'scalar',
      '--mode', 'reverse', '--direction', 'forward', '--quiet'], candidates=2)
for batch in ('3','7','17','2048'):
    for threads in ('1','4'):
        for side, interval in [('lo', f'{key:x}:{key+30:x}'), ('hi', f'{key-30:x}:{key:x}')]:
            check(f'reverse-batched-edge/{batch}/{threads}/{side}',
                  ['--range', interval, '--address', address, '--mode', 'reverse', '--threads', threads,
                   '--batch', batch, '--segment-size', '64', '--quiet'])

passed = sum(r['passed'] for r in rows)
print(f'{binary.parent.name}: {passed}/{len(rows)} public-fixture cases passed; {len(rows)-passed} failed')
for r in rows:
    if not r['passed']: print('FAIL', r['name'], r.get('stderr', ''))
sys.exit(passed != len(rows))
