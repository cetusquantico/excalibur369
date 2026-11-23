# CPU compatibility and extension points

This release targets Linux x86-64 with GCC/Clang and C++17. A default build
keeps the baseline code separate from the SSSE3 and AVX2 translation units.
The SIMD objects explicitly disable LTO to keep those instructions outside
baseline callers even when LTO is requested for the library.

## Selection

| Available compiled code and CPU/OS support | Selected hash implementation |
|---|---|
| Baseline x86-64, without usable SSSE3 | Scalar |
| SSSE3, including the local i5-3470 | SSE, four lanes |
| AVX2 with OS-managed XMM and YMM state | AVX2, eight lanes |
| AVX-512 hardware | AVX2 when available; no AVX-512 implementation exists |

Feature detection checks CPUID XSAVE, OSXSAVE and AVX before executing XGETBV.
AVX2 additionally requires the AVX2 feature and XCR0 state. A CPU instruction
flag alone is insufficient. Explicit requests fall back to a supported compiled
tier; the effective implementation is printed rather than assuming the request
was fulfilled.

```sh
./build/kh71 --cpu-info
./build/kh71 --build-info
./build/kh71 --self-test
```

The self-test uses published known vectors and synthetic buffers, executes every
usable compiled hash backend, and returns nonzero on disagreement. It does not
replace CTest, the independent coincurve oracle, or actual hardware validation.

## Portable and local builds

Keep EX369_NATIVE=OFF when distributing a binary. EX369_NATIVE=ON allows the
compiler to emit instructions for the build CPU throughout the program;
runtime hash dispatch cannot make that binary portable again. User-supplied
compiler flags can also override the portable baseline.
[GCC documents this distinction](https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html).

EX369_SIMD=OFF provides a build containing only the scalar hash implementation.
It is useful for validating the baseline and troubleshooting a compiler issue.

Binary compatibility also depends on the operating system, dynamic loader and
libc/libstdc++ versions. An instruction-compatible CPU alone does not establish
that a binary built on a newer distribution will run on an older one. Building
from source on the destination remains the documented installation path.

## Validation and limits

The local physical CPU validates scalar and SSE execution. QEMU user emulation
is used to test qemu64, Nehalem, Haswell-noTSX, EPYC-v4, EPYC-Genoa-v1 and
SapphireRapids-v1 models, including the selection and execution of the AVX2
implementation. The solved puzzle 39 fixture also runs through the real engine
under these models; no unsolved range is used for validation.
[QEMU supports selecting a virtual CPU model](https://www.qemu.org/docs/master/user/main.html).

Emulation checks are correctness evidence, not measurements of a real modern
processor's performance, cache, topology, frequency or thermals. The topology
reported under user emulation comes from the host Linux system. Hosted CI is
configured for these models but only produces evidence after the workflow runs.

Hybrid Intel processors, heterogeneous cores, NUMA systems and large AMD systems
need measurements on actual hardware. Current affinity honors allowed logical
CPUs and physical-core identities; it does not rank cores by speed, account for
cgroup CPU-time quotas, or tune for NUMA. ARM, macOS and native Windows are not
validated release platforms.

## Adding support later

A new implementation should remain in a separate translation unit, have explicit
CPU and OS feature gates, be listed as compiled/usable accurately, and match the
independent known vectors before being selected automatically. Tests should cover
both the supported path and fallback, including incomplete OS vector state.
Performance claims require hardware measurements with the same workload.

## Reproducible diagnostics

```sh
mkdir -p bench-results
python3 bench/run_diagnostics.py --binary build/hash_diagnostics \
  --reps 5 --output bench-results/hash-report.json
```

This diagnostic hashes a fixed number of synthetic 33-byte buffers. It stores
raw samples, discarded warmup count, median/spread, build description, CPU
capabilities, allowed CPUs and binary checksum. Existing reports are refused.
It does not search a range, select targets, measure full candidates per second,
or compare traversal modes. Run diagnostics after builds and tests have finished.

The older microbench now reports nanoseconds per operation rather than converting
wall time to estimated cycles using an assumed fixed 3.5 GHz clock. Historic
cycle estimates in the performance ledger are not hardware-counter measurements.
