# Changelog

## 0.1.0 — CPU release candidate

Known release blocker: non-aligned ranges can spend impractical time discarding
out-of-range segments before reporting any candidates. Added separate discard
counters and zero-work diagnostics; the scheduling limitation is still open.
See [the reverse-range incident](docs/INCIDENTE_REVERSE_INTERVALO.md).

- Adopted the MIT open source license.
- Added private confirmed-result JSONL, synchronized writes and exclusive CLI
  locks for checkpoint/output ownership, including path-collision checks.
- Added --self-test, --build-info, scalar-only builds and CPU/OS capability tests.
- Isolated SIMD objects from LTO and added emulated old/new CPU validation.
- Expanded solved-fixture reverse checks, signal/resume tests and documented
  batched ordering and the limits of the proposed two-machine 50% claim.
- Added reproducible synthetic hash diagnostics; microbench timings now use
  nanoseconds rather than an assumed 3.5 GHz conversion.

- Corrected clipping when both segment endpoints lie outside a valid range.
- Preserved manual fixed-byte position/value associations and included them in
  checkpoint compatibility checks.
- Added checked integer parsing, scalar bounds, singleton ranges, and correct
  index arithmetic at 127 free bits.
- Added strict checkpoint parsing, schema/version validation, progress bounds,
  integrity checks, unique temporary files and file/directory synchronization.
- Restored cumulative checkpoint candidate counts and elapsed time. Legacy
  aligned checkpoints without fixed bytes remain compatible.
- Report confirmed hits immediately and return failures for invalid verification,
  checkpoint write errors and execution failures.
- Respect CPU affinity restrictions and avoid a tree allocation for normal
  scheduler completions. Cryptographic arithmetic and hashing kernels are unchanged.
- Portable compilation by default, optional native builds and source-root link.
- Added CLI/coverage regressions, the solved public puzzle 39 fixture, required
  independent release verification, and a GitHub Actions validation matrix.

AVX2 is exercised under emulation; physical AVX2 hardware remains unvalidated.
Hosted CI has been prepared; it will run after the repository is published. See the audit report
for test evidence and the measured performance comparison.
