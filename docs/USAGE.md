# EXCALIBUR369 — Usage notes

## Position numbering for fixed bytes

By default `--fixed-positions` counts from the **first active byte** of the
range — the first byte the range actually lets vary. For puzzle 71 that is
big-endian byte 23, so:

```
--fixed-positions 0,1,2      →  absolute bytes 23, 24, 25
--fixed-positions 1,2,3      →  absolute bytes 24, 25, 26
```

`--fixed-index-base absolute` switches to raw 0..31 indexing into the 32-byte
scalar. A position that lies entirely inside the range prefix is rejected with
an explanatory error rather than silently ignored.

## How many values a fixed byte really has

A fixed byte only enumerates the bits the range leaves free. On puzzle 71:

| byte | bits it covers | free bits | legal values |
|---|---|---|---|
| 23 | 64..71 | 6 (bits 70,71 are prefix) | 64 (`0x40`..`0x7F`) |
| 24 | 56..63 | 8 | 256 |
| 25 | 48..55 | 8 | 256 |

So `--fixed on --fixed-count 3` yields `64 × 256 × 256 = 2²²` patterns of
`2⁴⁸` keys each — exactly `2⁷⁰`, the whole puzzle space, covered once.

Check it yourself before a long run:

```bash
kh71 --puzzle 71 --address <ADDR> --fixed on --fixed-count 3 --dry-run
```

A manual `--fixed-values` byte that contradicts the prefix is refused:

```
error: fixed value AA at byte 23 is outside the range: only the low 6 bit(s)
       are free there, so the byte must be 40..7F
```

## Global vs local traversal

They are separate on purpose:

* `--mode` orders the **segments** (sequential / reverse / random-no-repeat).
* `--direction` walks **inside** a segment (`P += G` or `P -= G`).

Random ordering is applied at the segment level so the inner loop stays a
contiguous incremental walk — randomizing individual keys would destroy the
incremental EC path and cost roughly an order of magnitude.

`--mode reverse` alone also flips the local direction to `P -= G`, since that
is what "search backwards" means; pass `--direction` explicitly to override.

The batched engine reverses each internal lane, with a separate remainder.
Neither batching nor multiple workers implies a globally descending temporal
sequence. Fixed-pattern rotation is independent. See [reverse semantics](REVERSE_MODE.md)
for the implications for two machines and probability claims.

## Round-robin

```bash
--round-robin on --slice-keys 1073741824
```

Each pattern is worked for `slice_keys` candidates, then the next pattern takes
over; earlier patterns are resumed later **without repeating** anything.
`slice_keys` is rounded to a whole number of segments — the plan prints the
effective value.

With `--round-robin off` (the default) a pattern is exhausted before the next
one starts.

## Checkpoints

```bash
--checkpoint run71.json --checkpoint-interval 30      # write every 30s
--checkpoint run71.json --resume                      # continue
```

The file records the puzzle/range, traversal mode, direction, seed, fixed-byte
mask and value, rotation mode, current pattern, current segment, local offset,
total candidates and round-robin state, plus a `config_hash`. Resuming with a
different configuration is refused rather than silently corrupting coverage.

Version 2 uses a unique temporary file, `fsync` of the file, atomic rename and
`fsync` of the directory. A checksum detects accidental corruption. Loading
requires a complete valid schema and compatible configuration; missing files,
unsupported versions, invalid progress and changed manual values are errors.

The CLI holds a nonblocking advisory lock from before checkpoint loading until
exit, rejecting another writer for the same path. The adjacent .lock file is
kept permanently; do not delete it while a process is running. The kernel releases
the lock when the process exits or is killed. This protects cooperating CLI
instances on filesystems with working flock semantics, not unrelated writers.
Library callers must provide equivalent ownership. Prefer a local filesystem.

Version 1 checkpoints can be resumed only
for aligned ranges without fixed bytes; other legacy configurations must restart
because the previous implementation could omit coverage. Original files are read
without changes during `--dry-run`.

Candidate and time totals accumulate across resumes. The final performance report
measures the current invocation. Progress counts include candidates repeated when
an interrupted segment is restarted, so percentage and ETA are estimates.

## `--limit` overshoots

`--limit` is a benchmarking aid, checked at segment boundaries. With 4 threads
and 4.19M-key segments a `--limit 20000000` typically stops nearer 50M. It is
not a precise cutoff.

## Threads and affinity

```bash
--threads 4 --affinity on        # pin to one CPU per physical core
--smt on                         # allow SMT siblings (off by default)
```

SMT is **not** assumed to help. On a hyperthreaded machine, measure both:

```bash
BENCH_REPS=9 ./bench/run_bench.sh "no-smt" -- --threads 4
BENCH_REPS=9 ./bench/run_bench.sh "smt"    -- --threads 8 --smt on
```

## Target files

```bash
kh71 -f targets.txt --range ...
```

One entry per line, either a Base58Check P2PKH address or a 40-hex HASH160;
blank lines and `#` comments are ignored. A line that is neither is a hard
error naming the line number - silently skipping it would mean searching for
the wrong thing for days.

## Progress output

```bash
--status 9        # a line every 9 seconds
```

```
[t+00:00:10] 6.700 M cand/s | done 66.585 M | 0.000024% | ETA 1.3y
```

The rate is instantaneous (measured over the last interval), not the running
average; the final report at exit carries the average.

## Verifying a key by hand

```bash
kh71 --verify 0000...02a1b3c7
```

Prints the private scalar, WIF, compressed pubkey, HASH160 and P2PKH address
via the independent verifier. Invalid scalars or mismatched targets return a
nonzero exit code. Confirmed search hits are printed and flushed immediately.

## Confirmed result journal

The optional --output PATH appends independently confirmed results as one JSON
object per line, including the configuration fingerprint, public derivation and
private test scalar. The same option works with --verify for a supplied known key.
The operation field distinguishes a supplied-key verification from a confirmed
search match; verification alone does not claim a match against a target.
New files use 0600 permissions and each confirmed record is synchronized to disk.
Existing output must also have private permissions. Output failures return nonzero.

The CLI holds an exclusive writer lock for this file too. Output, checkpoint,
locks and target inputs must have distinct paths. Symlinks, hard-linked run
files and nonregular files are refused. A truncated final line is an error:
preserve that file for inspection and select a new output path. This does not
validate the schema of pre-existing complete lines.

An interrupted write or a repeated segment can yield a partial record or a
duplicate result. JSONL is not a distributed transaction with the checkpoint.
Dry-run does not create result or lock files. Results and locks are git-ignored.


## Local builds and portable binaries

The default build uses a portable baseline and runtime dispatch. Compile with
`-DEX369_NATIVE=ON` only for a binary intended for the same CPU. The effective
hash backend is shown by `--cpu-info` and the plan; a requested unavailable
backend falls back to a supported one. AVX-512 is not implemented in this release.

Linux CPU topology and affinity respect the current process's permitted CPUs.
`--threads` accepts 1..1024, `--batch` 1..65536, and periodic intervals at most
86400 seconds. Default threads still select the available physical cores.
The scheduler supports at most 64-bit ticket counts and 127 free enumeration bits
per pattern; configurations exceeding those limits are rejected.

Use --self-test for bounded known-answer checks and --build-info to record the
compiler and build configuration. [CPU compatibility](CPU_COMPATIBILITY.md)
describes scalar-only builds, CPU emulation and future implementation requirements.
