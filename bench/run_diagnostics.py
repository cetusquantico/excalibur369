#!/usr/bin/env python3
"""Reproducible synthetic hash diagnostics, never a key-search benchmark."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess

def bounded(lo, hi):
    def parse(value):
        try:
            number = int(value)
        except ValueError:
            raise argparse.ArgumentTypeError('must be an integer') from None
        if not lo <= number <= hi:
            raise argparse.ArgumentTypeError(f'must be in {lo}..{hi}')
        return number
    return parse

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build/hash_diagnostics'))
    parser.add_argument('--reps', type=bounded(2, 30), default=5)
    parser.add_argument('--warmups', type=bounded(1, 5), default=1)
    parser.add_argument('--hashes', type=bounded(8, 67108864), default=4194304)
    parser.add_argument('--output', type=Path, required=True, help='new JSON report; existing files are refused')
    args = parser.parse_args()
    if args.hashes % 8:
        parser.error('--hashes must be a multiple of 8')
    if args.output.exists():
        parser.error('--output already exists; choose a new report path')
    binary = args.binary.resolve(strict=True)
    cli = binary.with_name('kh71')

    def invoke(argv):
        return subprocess.run(argv, check=True, capture_output=True, text=True, timeout=120).stdout

    self_test = invoke([str(cli), '--self-test'])
    build_flags = {}
    cache = binary.parent / 'CMakeCache.txt'
    if cache.is_file():
        wanted = {'CMAKE_CXX_FLAGS', 'CMAKE_CXX_FLAGS_RELEASE', 'CMAKE_CXX_FLAGS_DEBUG',
                  'CMAKE_EXE_LINKER_FLAGS', 'CMAKE_BUILD_TYPE', 'EX369_NATIVE', 'EX369_LTO',
                  'EX369_SIMD', 'EX369_FIELD', 'EX369_PGO'}
        for line in cache.read_text().splitlines():
            name = line.split(':', 1)[0]
            if name in wanted and '=' in line:
                build_flags[name] = line.split('=', 1)[1]
    metadata = {
        'schema': 1,
        'created_utc': datetime.now(timezone.utc).isoformat(),
        'workload': 'synthetic-33-byte-hash160-v1',
        'note': 'Hash-only diagnostic; does not measure full candidates/s or compare traversal modes.',
        'platform': platform.platform(),
        'allowed_cpus': sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'cmake_flags': build_flags,
        'build_info': invoke([str(cli), '--build-info']),
        'cpu_info': invoke([str(cli), '--cpu-info']),
        'self_test': self_test,
        'hashes_per_backend': args.hashes,
        'warmups_discarded': args.warmups,
        'repetitions': args.reps,
        'samples': [],
    }
    for iteration in range(args.warmups + args.reps):
        result = json.loads(invoke([str(binary), '--hashes', str(args.hashes)]))
        assert result['schema'] == 1 and result['workload'] == metadata['workload']
        assert result['samples'] and all(s['hashes'] == args.hashes and s['elapsed_seconds'] > 0 for s in result['samples'])
        if iteration >= args.warmups:
            metadata['samples'].append(result['samples'])
    rates = {}
    expected = {s['backend'] for s in metadata['samples'][0]}
    for sample in metadata['samples']:
        assert {s['backend'] for s in sample} == expected
        for s in sample:
            rates.setdefault(s['backend'], []).append(s['hashes'] / s['elapsed_seconds'])
    metadata['summary'] = {
        name: {'median_hashes_per_second': statistics.median(values),
               'minimum_hashes_per_second': min(values), 'maximum_hashes_per_second': max(values)}
        for name, values in rates.items()
    }
    # Exclusive creation also protects against a report appearing during the run.
    with args.output.open('x', encoding='utf-8') as out:
        json.dump(metadata, out, indent=2, allow_nan=False)
        out.write('\n')
    print(f'Saved {args.output}')
    for name, values in metadata['summary'].items():
        print(f"{name}: median {values['median_hashes_per_second']/1e6:.3f} M hashes/s "
              f"({values['minimum_hashes_per_second']/1e6:.3f}..{values['maximum_hashes_per_second']/1e6:.3f})")

if __name__ == '__main__':
    main()
