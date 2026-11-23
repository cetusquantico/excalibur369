# EXCALIBUR369

A CPU-first key hunter for **public Bitcoin puzzle ranges**.

Built for one number: **full valid candidates per second**, where a candidate
counts only after

```
private scalar → compressed secp256k1 pubkey → SHA-256 → RIPEMD-160 → target compare
```

No GPU in this phase. `Base58Check` never runs inside the hot loop.

> **Scope.** This tool is for the public Bitcoin puzzle addresses, whose ranges
> are published deliberately as a cryptographic challenge. It offers no
> advantage against ordinary keys. Even the published 2⁷⁰ range of puzzle 71
> takes millions of years to exhaust on one CPU at the rates documented here.
> Fixed-byte rotation changes visit order; covering all patterns still covers
> the entire original space.

*(Versão em português: [LEIAME.md](LEIAME.md))*

---

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
(cd build && ctest --output-on-failure)
```

Run `./build/kh71`. For an optional root shortcut, configure with
`-DEX369_ROOT_LINK=ON`. Builds otherwise leave the source directory alone.

Validated on Linux x86-64 with GCC and Clang (C++17 and `unsigned __int128`),
CMake 3.16+, and POSIX threads. No third-party runtime libraries are required.
Other operating systems and architectures are not validated for this release.
(`tests/reference_check.py` additionally cross-checks against `coincurve`
/libsecp256k1 when that module is installed; it self-skips otherwise.)

### Field representation

```bash
cmake -S . -B build -DEX369_FIELD=5x52   # default (unsaturated, faster)
cmake -S . -B build -DEX369_FIELD=4x64   # saturated reference
```

Both implement the same API, and **the whole test suite runs against whichever
is selected** — neither is a benchmark-only path.

## Quick start

```bash
./build/kh71 --cpu-info                       # what this CPU can do
./build/kh71 --puzzle 71 --address <ADDR> --dry-run    # show the plan, search nothing
./build/kh71 --puzzle 71 --address <ADDR> --mode sequential
```

## The CLI

```
kh71 --mode sequential
kh71 --mode reverse
kh71 --mode random --seed 12345

kh71 --fixed on --fixed-count 3 --fixed-positions auto \
     --fixed-rotation random-no-repeat

kh71 --fixed on --fixed-values AA,BB,CC --fixed-positions 1,2,3 \
     --round-robin on --slice-keys 1073741824
```

`--help` lists every flag. Highlights:

| flag | meaning |
|---|---|
| `--puzzle N` / `--range LO:HI` | what to search |
| `--address` / `--hash160` | a single target |
| `--file PATH`, `-f` | a file of targets: one P2PKH address or 40-hex HASH160 per line |
| `--status S` | print a progress line (rate, total, %, ETA) every S seconds |
| `--mode` | **global** segment ordering: sequential, reverse, random-no-repeat |
| `--direction` | **local** walk inside a segment: `P += G` or `P -= G` |
| `--fixed*` | the fixed-byte engine (fully switchable, `--fixed off` by default) |
| `--round-robin` / `--slice-keys` | optional pattern interleaving |
| `--checkpoint` / `--resume` | durable progress |
| `--output PATH` | private JSONL journal of confirmed results |
| `--self-test` / `--build-info` | validate this installation / report build metadata |
| `--simd` | force `scalar`/`sse`/`avx2`; default is the best the CPU supports |
| `--verify KEYHEX` | run the independent verifier on one key and exit |

Byte positions are numbered from the first **active** byte of the range by
default (so `1,2,3` means "the 2nd, 3rd and 4th bytes that the range lets vary");
`--fixed-index-base absolute` switches to raw 0..31 indices.

---

## What the design guarantees

**Complete traversal without duplicate work in an uninterrupted run.** Work is addressed by a monotonic
ticket that maps through *bijections* to (pattern, segment). Random-no-repeat is
a keyed Feistel permutation, so "no repeats" is structural and costs O(1)
memory, not a visited-set of size 2⁷⁰. Claiming work is a single atomic
`fetch_add` — there is no mutex on the hot path.

**A fixed byte that straddles the range prefix is handled correctly.** For
puzzle 71 the top active byte has only 6 free bits, so it has 64 legal values,
not 256. Getting this wrong would emit out-of-range keys *and* scan the same
space four times. `--fixed-count 3` on puzzle 71 gives exactly 2²² patterns ×
2⁴⁸ keys = 2⁷⁰ — the whole space, once.

**Resume never skips.** A segment interrupted half-way is deliberately left
incomplete, so the watermark stalls on it and a resume redoes it. Redoing a
little work is safe; skipping it silently is not.

**A hit is never trusted from the hot loop.** `verify_candidate()` recomputes
the candidate through a deliberately different code path — affine
double-and-add instead of the batched Jacobian engine, the generic streaming
hashes instead of the one-block specializations — and re-derives the address.
`FOUND` is printed only if that agrees.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full design.

---

## Tests

```
test_hash        hash/address known-answer vectors, one-block == generic
test_hash_simd   every SIMD backend vs scalar, bit for bit
test_ec          field laws, key→pubkey→hash160→address vectors,
                 forward (k,k+1,k+2) and reverse (k,k-1,k-2) traversal
test_coverage    every mode/rotation/round-robin setting visits its keyspace
                 EXACTLY once — no duplicates, no omissions
test_engine      the real hot loop finds planted keys in every configuration
test_checkpoint  resume loses nothing; an interrupted segment is redone
reference_check  cross-check against libsecp256k1 (independent implementation)
test_regressions range, input and configuration regressions
test_cpu        CPU/OS state gates and compiled-backend selection
test_reverse    reference enumeration order on tiny synthetic intervals
test_cli        command-line behavior and errors
test_run_files  result persistence, locks, signals and reverse resume
public_puzzle39 solved public fixture, including real reverse execution
self_test       installed known-answer diagnostic
hash_diagnostics bounded synthetic hashing and reference agreement
```

---

## Performance

Measured, not asserted. See [docs/BENCHMARKS.md](docs/BENCHMARKS.md) for the
full ledger — **including the six optimizations that were tried, measured, and
rejected because they were slower.**

On an Intel i5-3470 (Ivy Bridge, 4 cores, no SMT, **no AVX2**):

| step | M cand/s | vs prev | vs V0 |
|---|---|---|---|
| V0 baseline | 2.550 | — | — |
| + SSE 4-way multi-buffer HASH160 | 5.896 | +131.2% | +131.2% |
| + batch 512 | 6.125 | +3.9% | +140.2% |
| + CPU affinity | 6.481 | +5.8% | +154.2% |
| **+ 5x52 unsaturated field** | **7.095** | **+7.4%** | **+178.2%** |

Benchmark methodology: `bench/run_bench.sh` runs the same workload N times and
reports the **median**, because single runs on this box vary by ~±3%.
```bash
BENCH_REPS=9 ./bench/run_bench.sh "my change"
./build/microbench          # per-stage cost breakdown
```

---

## Release validation

Known limitation: large non-aligned intervals can finish millions of scheduler
segments without checking any candidates. Separate discard counters now explain
this state; the planning limitation remains unresolved. See
[the reverse-range incident](docs/INCIDENTE_REVERSE_INTERVALO.md).

The CPU release is version 0.1.0. See [CHANGELOG.md](CHANGELOG.md) and
[the CPU audit](docs/AUDITORIA_CPU_2026-09-12.md) for fixes, evidence and limits.
The [release validation follow-up](docs/VALIDACAO_RELEASE_CPU.md) records the
additional reverse, persistence and CPU compatibility checks.
GPU support is outside this release.

Reverse orders segments and reverses their internal lane walks. Batched and
multithreaded execution is not a single globally descending sequence. Two
independent instances do not coordinate their coverage. See
[reverse semantics and the 50% claim](docs/REVERSE_MODE.md).

For release checks, install `tests/requirements.txt` in a Python virtual
environment and configure with `-DEX369_REQUIRE_REFERENCE=ON`. CI requires
the independent oracle and checks both field representations with GCC/Clang.
The solved puzzle 39 fixture is tested in small windows around its known key.

Builds are portable by default. `-DEX369_NATIVE=ON` is an optional local CPU
optimization; a binary built that way may fail on another processor.
[Contributing](CONTRIBUTING.md) describes validation and benchmark expectations.

[CPU compatibility](docs/CPU_COMPATIBILITY.md) documents modern processors,
emulation, fallback, diagnostic reports and future extension points. Historical
rates above are workload-specific; they are not hardware-independent guarantees.

## Credits

Built on ideas and tooling from [keyhunt](https://github.com/albertobsd/keyhunt) by Luis
Alberto (albertobsd), used under the MIT License — Copyright (c) 2020 Luis Alberto.

## Support

Self-funded, spare-time work. A GPU port is the next big step and needs hardware. If this is
useful to you and you'd like to help, Bitcoin donations are welcome, with no promise of
anything in return:

`bc1qhsq406jt6ghkzmhpnqe4r2s639t6mtrhj6zx03`

## License

This project is open source under the [MIT License](LICENSE).
