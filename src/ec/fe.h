// EXCALIBUR369 - field arithmetic selector.
//
// Two interchangeable representations of Fp implement the same API. Pick one at
// configure time with -DEX369_FIELD=5x52 (default) or -DEX369_FIELD=4x64; the
// whole test suite runs against whichever is selected, so both stay honest.
//
// See docs/BENCHMARKS.md for the measured comparison.
#pragma once

#if defined(EX369_FIELD_4X64)
#include "fe4x64.h"
#else
#include "fe5x52.h"
#endif
