# Contributing to EXCALIBUR369

The project uses the [MIT License](LICENSE); contributions use that same license.

Contributions should preserve complete coverage of the configured public puzzle
range. Use solved public fixtures or synthetic targets in tests.

## Build and validate

Linux x86-64 is the validated platform. Use GCC or Clang with C++17 and
`unsigned __int128`, CMake 3.16+, and Python 3 for the CLI tests.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r tests/requirements.txt
cmake -S . -B build -DEX369_NATIVE=OFF -DEX369_REQUIRE_REFERENCE=ON
cmake --build build --parallel 2
(cd build && ctest --output-on-failure)
```

Also validate `-DEX369_FIELD=4x64` in a separate build directory. Tests should
check observable behavior against an independent expected result. Coverage tests
must include the real engine, partial ranges, reverse traversal, masks, and resume.
The fixture for puzzle 39 is already solved and tests only small known windows.

## Performance changes

Provide CPU model, compiler and flags, thread count, backend, exact workload,
candidate count and raw measurements. Compare before and after binaries with
identical flags; interleave runs and report the median and spread. Run builds
and tests before benchmarking, so they do not compete for the same CPU.

An improvement must pass the full correctness suite for both field formats and
the independent oracle. A faster result with missing candidates is a failure.
Do not claim an improvement smaller than the measurement variation. Backend
results require hardware that actually supports that instruction set.

## Pull requests

Explain the problem, resulting behavior, validation, and any compatibility
change. Keep generated RIPEMD source reproducible using
`python3 tools/gen_ripemd_core.py`. Include the regression for a correctness fix.
Do not commit personal checkpoints, output files, credentials, or build artifacts.

The CPU release does not include GPU support. Discuss larger architectural
changes before implementing them so the coverage and resume contract stays clear.

See [CPU compatibility](docs/CPU_COMPATIBILITY.md) for instruction gating,
emulated versus physical validation, and reproducible synthetic diagnostics.
See [reverse semantics](docs/REVERSE_MODE.md) before making ordering or
two-machine probability claims.
