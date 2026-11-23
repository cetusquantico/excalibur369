# EXCALIBUR369 — Benchmarks

**Rule for this project: no optimization is claimed without a measurement.**
Four of the changes tried below turned out to be *slower* and were rejected.
They are recorded here because a rejected optimization is as useful to know
about as an accepted one.

## Machine

```
Intel Core i5-3470 (Ivy Bridge), 4 physical cores, no SMT
ISA: ssse3 sse4.2 avx     — NO avx2, NO avx512, NO bmi2, NO adx, NO sha-ni
L1d 32KB / L2 256KB per core / L3 6MB shared
measured clock ~3.5 GHz (calibrated with a dependent-add chain)
gcc 15.3.0, -O3 -march=native
```

**Important limitation:** this CPU predates AVX2. The AVX2 8-way backend is
compiled and wired into runtime dispatch, but it **cannot be executed or
benchmarked on this machine**. Its correctness is likewise untested here —
`tests/test_hash_simd.cpp` says so explicitly rather than assuming it passes.
The SSE 4-way backend *is* exercised and validated.

## Methodology

`bench/run_bench.sh` runs an identical workload N times and reports the
**median**, with the observed range. Single runs on this box vary by roughly
±3%, so any claim below 3% is treated as noise. Where a result was close,
variants were built as separate binaries and run **interleaved**, so that
thermal drift could not manufacture a result.

Workload: puzzle 71, unmatchable target, 6M-candidate limit, 4 threads.

---

## The ledger

Measured **interleaved**, 3 rounds x 3 reps per configuration, same binary
throughout (the settings differ, not the build), nothing else running:

| step | median M cand/s | vs previous | vs V0 | verdict |
|---|---|---|---|---|
| **V0** scalar hashing, batch 256, no affinity | **2.550** | — | — | baseline |
| **V1a** + SSE 4-way multi-buffer HASH160 | **5.896** | **+131.2%** | +131.2% | kept |
| **V1b** + batch 512 | **6.125** | **+3.9%** | +140.2% | kept |
| **V1c** + CPU affinity | **6.481** | **+5.8%** | +154.2% | kept |
| **V2** + 5x52 unsaturated field | **7.095** | **+7.4%** | **+178.2%** | kept |

Per-round spread was under 1.5% for every row, e.g. V1c across the three
rounds: 6.419 / 6.499 / 6.481.

Two further changes were made for robustness rather than speed, and are not
claimed as gains:

* **in-place Montgomery batch inversion** — drops the third scratch array;
* **batch-normalized lane anchors** — the anchors were built with affine adds,
  i.e. `B-1` full field inversions per segment. Irrelevant for large segments
  (~0.2%), but with `--segment-size` close to `--batch` the lane length falls to
  1 and it becomes an ~18x cliff. Now one inversion covers all `B` anchors.

Rejected:

| change | result | verdict |
|---|---|---|
| Unrolled SHA-256, rotating registers, in-place 16-word schedule | 1468 vs **1259** cyc/block | **rejected — 17% slower** |
| LTO | 6.404 vs **6.752** | **rejected — 5.2% slower** |
| PGO | 6.553 vs **6.752** | **rejected — 2.9% slower** |
| Dedicated 10-multiply `fe_sqr` | 6.721 vs 6.742 | **rejected — no effect**, reverted for simplicity |
| Comba/product-scanning `fe_mul` | 97.2 vs **91.8** cyc | **rejected — slower** |
| Fused 2-pass EC step (vs 4-pass) | 6.550 vs 6.630, interleaved | **no speedup**; kept only for its smaller footprint, and labelled as such |

A methodology note worth recording: two benchmark runs were once left executing
concurrently, which put a 3.4 M outlier into an otherwise ~6.5 M sample. The
median absorbed it, but the range column is what exposed it. Report the range,
not just the median.

An early reading suggested "PGO +4.8%". Repeating it with medians over 7 runs
showed the opposite. That is exactly why the median harness exists.

---

## Where the time goes (per candidate, 1 core, B=512)

```
EC step only              835.4 cyc   56%
+ serialize pubkey         39.7 cyc    3%
+ hash160 (SSE x4)        602.3 cyc   40%
+ target compare           10.8 cyc    1%
                        ---------
                         1488.1 cyc  ->  2.35 M cand/s/core
```
Engine measured 2.32 M cand/s/core — the model and the engine agree.

### Field arithmetic (throughput, 8 independent chains)

Measuring a *dependent* chain instead understates throughput by ~2x; these
numbers use 8 independent accumulators, matching how the EC loop issues work.

```
fe_mul   91.8 cyc      (512-bit product 60.5 + reduce512 28.1)
fe_sqr   78.9 cyc
fe_add   16.9 cyc
fe_sub   14.6 cyc
fe_inv   27054 cyc     (amortized to ~53 cyc/candidate at B=512)
```

The EC step costs `6·mul + 1·sqr + 7·sub + inv/512` = **787 cyc predicted**
vs **835 measured**. The model holds, so the EC step is ~66% `fe_mul`.

### Multi-buffer hashing

```
scalar  x1   1.34 M hash160/s   2612 cyc/hash    1.00x
sse-x4  x4   5.95 M hash160/s    588 cyc/hash    4.45x
avx2-x8 x8   -- not executable on this CPU --
```

### Batch size sweep

```
16    2.924        192   5.738        768   6.384
32    4.026        256   6.092       1024   6.357
64    5.007        384   6.085       1536   6.410
96    5.477        512   6.371       2048   6.489
128   5.532                          4096   6.336
```
512 was chosen: it is at the plateau *and* had the tightest spread across
repeats (6.26–6.33 vs 5.73–6.13 at 256 and 5.42–6.42 at 2048).

### Thread scaling (affinity on)

```
1 thread   2.317 M    1.00x
2 threads  4.521 M    1.95x
3 threads  6.384 M    2.76x
4 threads  6.580 M    2.84x
```
Scaling flattens at 4 threads; single-core throughput matches the per-stage
model exactly, so this is contention for shared resources (and other load on
the box), not a per-core inefficiency.

---

## The 5x52 field: what was projected vs what happened

The saturated 4x64 representation pays a serialized `adc` chain on every carry.
The standard fix is an **unsaturated 5x52 representation**: 52 value bits per
64-bit limb leaves 12 spare bits, so partial products accumulate inside a
128-bit register with no carry chain, and carries are paid once at the end.

It was implemented (`src/ec/fe5x52.h`), and both representations are kept and
selectable so the comparison stays reproducible:

```bash
cmake -S . -B build        -DEX369_FIELD=5x52   # default
cmake -S . -B build-4x64   -DEX369_FIELD=4x64
```

The **whole test suite runs against whichever is selected**, and both pass all
7 suites, so this is not a benchmark-only switch.

### End-to-end (interleaved, 4 rounds x 3 reps)

| field | median M cand/s | per round |
|---|---|---|
| 4x64 saturated | 6.606 | 6.541 / 6.634 / 6.706 / 6.578 |
| **5x52 unsaturated** | **7.095** | 7.091 / 7.127 / 7.066 / 7.100 |
| | **+7.4%** | 5x52 won every round, ranges did not overlap |

### Per operation (throughput, 8 independent chains)

| | 4x64 | 5x52 | |
|---|---|---|---|
| `fe_mul` | **98.3 cyc** | 119.6 cyc | 5x52 **worse** in isolation |
| `fe_sqr` | 79.4 cyc | 77.1 cyc | ~equal |
| `fe_add` | 15.9 cyc | **12.6 cyc** | -21% |
| `fe_sub` | 16.9 cyc | **14.1 cyc** | -17% |
| `fe_inv` | 27038 cyc | **22636 cyc** | -16% |
| EC batch step | 3.56 M pts/s | **3.93 M pts/s** | +10% |
| EC step in the engine | 831.0 cyc | **777.6 cyc** | -6.4% |

### The projection was wrong, and it is worth saying why

This document previously projected `fe_mul` 92 -> ~40 cycles and **+25%**
end-to-end. The real outcome was `fe_mul` getting *slower* in isolation and
**+7.4%** end-to-end. Two things were mis-reasoned:

1. **The adc chains did not disappear, they moved.** Accumulating into an
   `unsigned __int128` still compiles to `add`/`adc` pairs with a dependency
   between them. 5x52 removes the carry chain *between limbs of a product*, not
   the one inside the accumulator.
2. **5x52 issues more multiplies**: 25 partial products plus 5 reduction
   multiplies, against 16 + 5 for 4x64. At roughly 4 cycles per `mulq` on this
   core that alone accounts for the isolated `fe_mul` regression.

The gain that did materialize came from somewhere else entirely: `fe_add`,
`fe_sub` and `fe_inv`, where the spare bits genuinely remove carry work, plus a
better register profile in real code.

One implementation detail mattered a great deal. The first version materialized
all nine 128-bit product columns in an array before folding them; that needs
~18 registers for the columns alone, the compiler spilled to the stack, and it
measured **119.5 -> no better than 113** cycles with the EC step barely moving.
Restructuring it as a **single interleaved pass** - high columns reduced to five
64-bit words first, then folded into the low columns one at a time, keeping one
128-bit accumulator live - is what made the representation pay off at all.

Note also that the isolated `fe_mul` benchmark *disagrees* with the engine
measurement (5x52 is 22% worse isolated, 6.4% better in the EC step). Eight live
`Fe` accumulators is 40 words for 5x52 against 32 for 4x64, so the microbenchmark
spills where real code does not. **When a microbenchmark and an end-to-end
measurement disagree, the end-to-end one is the one that pays.**

## What is left

Earlier drafts estimated percentage gains for unimplemented arithmetic changes.
Those estimates were not validated experiments and have been removed. Future
performance claims need repeated measurements and independent correctness checks.


## CPU audit comparison — 2026-09-12

Before and after binaries were compiled from the preserved initial source and
corrected source with GCC 15.3.0, Release `-O3 -DNDEBUG`, `EX369_NATIVE=ON`,
and the 5x52 field. The i5-3470 ran four pinned threads, SSE, batch 2048 and
4,194,304-candidate segments. Each execution processed exactly **16,777,216**
candidates in a bounded range with a synthetic zero digest. Builds and tests
finished before this comparison. The binary order was alternated across six pairs.

| Pair | Before (M candidates/s) | After (M candidates/s) |
|---|---|---|
| 1 | 9.091 | 9.158 |
| 2 | 9.080 | 9.191 |
| 3 | 8.798 | 8.263 |
| 4 | 8.830 | 9.066 |
| 5 | 8.877 | 8.803 |
| 6 | 8.726 | 9.004 |
| Median | **8.8535** | **9.0350** |

The median difference is **+2.05%**, within the observed
variation. This experiment does **not establish a throughput improvement**.
The intervals overlap: 8.726–9.091 M/s before and
8.263–9.191 M/s after. No speedup is claimed for the
correctness fixes. The earlier ledger uses a different workload; compare the
before/after values within each experiment.

The scheduler now avoids a temporary tree allocation on in-order completion.
That removes bookkeeping work but is a small fraction of the cost at the default
segment size. Affinity now respects the permitted CPU set, preventing incorrect
placement in restricted environments. The EC and hashing kernels are unchanged.

### Next measurements

Validate AVX2 on compatible hardware against the independent oracle before
publishing performance numbers for it. Compare thread counts and batch sizes on
each new CPU; this audit does not establish a universal optimum. Profile the
whole pipeline and accept changes only with identical candidate coverage and
repeatable gains outside normal measurement variation. GPU work is a separate
future phase.

### Release diagnostics follow-up

The CPU release adds [reproducible synthetic hash diagnostics](CPU_COMPATIBILITY.md)
with raw JSON samples, warmup, build metadata and binary checksum. This diagnostic
is separate from the full pipeline and does not establish a candidates/s gain.
The legacy shell harness still uses an approximate limit; consult actual completed
work before comparing samples. It is not the exact-work harness used for the
audit table above.

The microbench now reports measured nanoseconds per operation. Historic cycle
figures in this document used an assumed 3.5 GHz conversion; they were not actual
hardware-counter readings. AVX2 correctness is now checked under emulation, with
physical modern-CPU measurements still outstanding.
