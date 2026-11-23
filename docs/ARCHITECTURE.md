# EXCALIBUR369 — Architecture

CPU-first key hunter for **public Bitcoin puzzle ranges**. No GPU in this phase.

The design priority order is the one that was set for the project, and it is
reflected in the code and in the test suite:

1. **Correctness**
2. **No duplicates, no omissions**
3. **Modular architecture**
4. **CPU performance**

---

## 1. Keyspace model (bit-level, not byte-level)

The search space is defined as

```
S = { k in [range_lo, range_hi] : (k & mask) == (value & mask) }
```

Internally the engine works at **bit** granularity. A fixed *byte* is just eight
pinned bits, so partial-bit masks are already a configuration change rather than
a rewrite.

Three groups of bits exist:

| group | source | example (puzzle 71) |
|---|---|---|
| **prefix** | dictated by the range | bits 70..255 |
| **pinned** | dictated by `mask`/`value` | whatever `--fixed-*` selects |
| **free** | everything else — the search space | the rest of bits 0..69 |

Free bits are collected LSB-first into **maximal contiguous runs**. An
enumeration index `i ∈ [0, 2^F)` is scattered across those runs to form a scalar.

### A fixed byte can straddle the prefix

For puzzle 71 the top active byte (big-endian byte 23) covers bits 64..71, but
only bits 64..69 are free — bits 70..71 belong to the range itself. That byte
therefore has **2^6 = 64** legal values, not 256.

`FixedSlot` records this per position (`free_bits`, `prefix_value`). Getting it
wrong would both emit out-of-range keys and make rotation revisit the same
keyspace four times. `tests/test_coverage.cpp` pins this behaviour.

Manual `--fixed-values` that contradict the prefix are rejected with a message
naming the legal range, rather than being silently masked.

---

## 2. Mask classification and the incremental fast path

`KeySpace::finalize()` classifies the free-bit runs:

| class | shape | inner step |
|---|---|---|
| `CONTIGUOUS_FAST` | one run starting at bit 0 | `P += G` (stride 1) |
| `STRIDED` | one run starting at bit *b* | `P += 2^b·G` (constant stride) |
| `GENERIC` | several runs | inner run strided; outer needs a fresh `kG` |

The key property: **the innermost run always has a constant stride**, so the
incremental point addition is preserved in every class. `GENERIC` only pays a
full `kG` when the outer part advances, which is exactly "compute kG only at the
start of a block".

Reverse traversal negates the stride point and walks each batch lane backward.
This does not imply a single globally descending sequence of candidates; see
[the reverse-mode contract](REVERSE_MODE.md).

---

## 3. Segmentation and the scheduler

All work is addressed by a single monotonic `uint64` **ticket**:

```
ticket ──► (pattern slot, segment ordinal)
             │              │
             │              └─► segment index   (identity | reversal | keyed PRP)
             └────────────────► pattern ordinal (identity | reversal | keyed PRP)
```

Every arrow is a **bijection**, which is what makes the coverage guarantees
structural rather than bookkeeping:

* claiming work is one `fetch_add` — **no mutex on the hot path**;
* two threads can never receive the same segment;
* the distribution is reproducible from `(seed, config)` within a run. There
  is no coordination of ownership or completed work between machines.

**Invariant:** `segment_size` is clamped to a power of two ≤ the inner run's
size, so a segment never straddles an outer block. Every segment is therefore
one arithmetic progression, and the fast path always applies.

### Random-no-repeat

`Prp` is a keyed balanced **Feistel network** with cycle walking. Because a
Feistel network is a bijection, `permute(0), permute(1), …` cannot repeat and
cannot skip — with **O(1) memory** instead of a visited set of size 2^F.

Random ordering is applied at the **segment** level (global traversal), so the
inner loop stays a contiguous incremental walk. That is the separation between
GLOBAL segment ordering and LOCAL in-segment traversal.

### Round-robin

Optional. When on, a ticket decodes as
`round = t / (patterns·slice_segs)`, and each pattern gets `slice_keys` worth of
segments before the next pattern is visited. When off, a pattern is exhausted
before moving on. Both are the same ticket space, so both inherit the same
no-overlap guarantee.

---

## 4. Checkpoint / resume

The authoritative state is the **ticket watermark** plus the handful of tickets
completed out of order. Everything else in the file is either a configuration
fingerprint or human-readable derived state.

A segment interrupted half-way is deliberately **not** marked complete: the
watermark stalls on it and a resume redoes it. Redoing a little work is safe;
skipping it would silently drop candidates. `test_checkpoint.cpp` asserts this
directly.

Resume refuses a checkpoint whose `config_hash` does not match the current run.
The fingerprint includes manual values and position order. Version 2 also verifies
a checksum over the canonical state, validates ticket bounds, and uses durable
file and directory writes. See [USAGE.md](USAGE.md) for legacy compatibility.

---

## 4b. Field representation

`fe.h` is a selector over two interchangeable implementations of Fp:

* **`fe5x52.h`** (default) — unsaturated 5 x 52-bit limbs. The 12 spare bits per
  limb let partial products accumulate in a 128-bit register without a carry
  chain. Values are kept *weakly normalized*: limbs in range and the value below
  2^256, but possibly >= p. Exactly two values are then congruent to zero (0 and
  p, since 2p > 2^256), which keeps `is_zero()` cheap. Anything needing a
  canonical value — serialization, parity, equality — normalizes a copy.
* **`fe4x64.h`** — saturated 4 x 64-bit limbs, always fully reduced. Simpler,
  and the reference the other must agree with.

`fe_mul`/`fe_sqr` in the 5x52 build are a *single interleaved pass*: the high
product columns are reduced to five 64-bit words first, then folded into the low
columns one at a time. Materializing all nine 128-bit columns first — the
obvious way — needs ~18 registers, spills to the stack, and measured slower than
the 4x64 code it was meant to beat.

Selected at configure time with `-DEX369_FIELD=`; the full test suite runs
against whichever is chosen.

## 5. Hot loop

Per segment, per thread:

```
1 × ec_mul_gen  →  lane 0 anchor
B-1 × point add →  the other lanes (spread across the segment)
then per step:
    serialize B compressed pubkeys
    hash160 them, `lanes` at a time, through the SIMD backend
    test against the target filter
    batch_step_affine: advance all B lanes by ±stride·G
```

`batch_step_affine` is deliberately **two passes** over the lane array, not
four. At B=512 the arrays exceed L1, so each extra pass is a full re-stream from
L2. `dx` is recomputed in pass 2 rather than stored, removing an entire array.

---

## 6. Hash layer and runtime dispatch

`MultiHash` backends compute `RIPEMD160(SHA256(pubkey))` for `lanes` inputs at
once. The scalar backend is the reference; **every SIMD backend is checked
bit-for-bit against it** in `tests/test_hash_simd.cpp`.

With the default portable build, each backend is compiled in its own translation
unit with its own ISA flags and selected at **runtime**. `EX369_NATIVE=ON` opts
out of this binary portability and enables instructions throughout the program.

The RIPEMD-160 SIMD core is **generated** (`tools/gen_ripemd_core.py`) because
its per-round rotate amounts must be compile-time immediates.

`Base58Check` never appears in the hot loop — only in the CLI and the verifier.

---

## 7. Verification through a separate execution path

The verifier shares field arithmetic and generic hash primitives with the engine;
its execution path is different, but it is not a separate cryptographic library.
The coincurve/libsecp256k1 tests provide the independent implementation check.

A hot-loop hit is **never** reported directly. `verify_candidate()` recomputes
the candidate through a deliberately different code path:

* affine double-and-add scalar multiplication (one `fe_inv` per addition),
  not the batched Jacobian engine;
* the generic streaming `sha256()` / `ripemd160()`, not the one-block
  specializations;
* re-encoded all the way to a Base58Check address.

Only if that agrees is `FOUND` printed. A disagreement prints a warning naming
the rejected scalar.

---

## 8. Layout

```
src/
  ec/         u256, field arithmetic, group ops, batch inversion
              fe.h selects fe5x52.h (default) or fe4x64.h
  hash/       sha256, ripemd160, hash160, multi-buffer dispatch
    simd/     vec_ops.h + per-ISA backends + generated cores
  traversal/  Feistel PRP (random-no-repeat)
  fixed/      mask engine, slot resolution, pattern rotation
  scheduler/  ticket scheduler
  checkpoint/ save / resume
  cpu/        capability detection, affinity
  verify/     independent verifier, Base58Check
  benchmark/  counters and reporting
  cli/        kh71
tests/        10 suites including CLI regressions and public puzzle 39
bench/        benchmark harness + microbenchmark
tools/        code generators
```
