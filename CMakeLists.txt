cmake_minimum_required(VERSION 3.16)
project(EXCALIBUR369 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Do not clobber a user-supplied CMAKE_CXX_FLAGS_RELEASE; only supply a default.
if(NOT CMAKE_CXX_FLAGS_RELEASE)
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -fno-plt")
endif()

option(EX369_NATIVE "Optimize for this CPU only (binary is not portable)" OFF)
option(EX369_SIMD "Build isolated x86 SIMD hashing backends" ON)
option(EX369_ROOT_LINK "Create ./kh71 symlink in the source directory" OFF)
option(EX369_REQUIRE_REFERENCE "Require coincurve for independent release tests" OFF)
if(NOT UNIX)
  message(FATAL_ERROR "This release requires a POSIX system and GCC/Clang with unsigned __int128")
endif()
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("int main() { unsigned __int128 n = 1; return int(n >> 64); }" HAS_UINT128)
if(NOT HAS_UINT128)
  message(FATAL_ERROR "A compiler with unsigned __int128 support is required")
endif()
# Measured OFF by default: LTO was 3.6% SLOWER here (6.39 vs 6.63 Mcand/s), and
# SIMD translation units stay outside LTO to preserve runtime ISA isolation.
option(EX369_LTO "Link-time optimization" OFF)
# Field representation: 5x52 (unsaturated, default) or 4x64 (saturated reference).
set(EX369_FIELD "5x52" CACHE STRING "secp256k1 field representation: 5x52 or 4x64")
set_property(CACHE EX369_FIELD PROPERTY STRINGS 5x52 4x64)
if(NOT EX369_FIELD STREQUAL "5x52" AND NOT EX369_FIELD STREQUAL "4x64")
  message(FATAL_ERROR "EX369_FIELD must be 5x52 or 4x64")
endif()

set(EX369_PGO "" CACHE STRING "Profile-guided optimization: 'gen', 'use', or empty")
set(EX369_PGO_DIR "${CMAKE_BINARY_DIR}/pgo" CACHE PATH "Where PGO profile data lives")
if(NOT EX369_PGO STREQUAL "" AND NOT EX369_PGO STREQUAL "gen" AND NOT EX369_PGO STREQUAL "use")
  message(FATAL_ERROR "EX369_PGO must be gen, use or empty")
endif()
if(EX369_PGO AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(FATAL_ERROR "The PGO flags in this release require GCC")
endif()

add_library(ex369 STATIC
  src/config.cpp
  src/ec/group.cpp
  src/hash/sha256.cpp
  src/hash/ripemd160.cpp
  src/cpu/cpu.cpp
  src/fixed/mask.cpp
  src/fixed/pattern.cpp
  src/traversal/prp.cpp
  src/scheduler/scheduler.cpp
  src/checkpoint/checkpoint.cpp
  src/verify/verify.cpp
  src/benchmark/stats.cpp
  src/io/run_files.cpp
  src/diagnostics/self_test.cpp
  src/engine.cpp
  src/hash/hash_multi.cpp
)

# --- multi-buffer hashing backends -------------------------------------------
# Each backend is compiled with its own ISA flags and selected at RUNTIME, so a
# binary built here still runs on a CPU without those instructions.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mssse3" HAS_SSSE3_FLAG)
check_cxx_compiler_flag("-mavx2"  HAS_AVX2_FLAG)

if(EX369_SIMD AND HAS_SSSE3_FLAG)
  target_sources(ex369 PRIVATE src/hash/simd/backend_sse.cpp)
  set_source_files_properties(src/hash/simd/backend_sse.cpp
                              PROPERTIES COMPILE_OPTIONS "-mssse3;-fno-lto")
  target_compile_definitions(ex369 PUBLIC EX369_HAVE_SSE)
endif()
if(EX369_SIMD AND HAS_AVX2_FLAG)
  target_sources(ex369 PRIVATE src/hash/simd/backend_avx2.cpp)
  set_source_files_properties(src/hash/simd/backend_avx2.cpp
                              PROPERTIES COMPILE_OPTIONS "-mavx2;-fno-lto")
  target_compile_definitions(ex369 PUBLIC EX369_HAVE_AVX2)
endif()
target_include_directories(ex369 PUBLIC src)
configure_file(src/build_info.h.in generated/build_info.h @ONLY)
target_include_directories(ex369 PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
find_package(Threads REQUIRED)
target_link_libraries(ex369 PUBLIC Threads::Threads)

if(EX369_FIELD STREQUAL "4x64")
  target_compile_definitions(ex369 PUBLIC EX369_FIELD_4X64)
endif()
message(STATUS "EXCALIBUR369 field representation: ${EX369_FIELD}")

if(EX369_NATIVE)
  check_cxx_compiler_flag("-march=native" HAS_NATIVE)
  if(HAS_NATIVE)
    # Applies to all targets. This explicitly opts out of binary portability.
    target_compile_options(ex369 PUBLIC -march=native)
  endif()
endif()
if(EX369_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
  if(NOT IPO_SUPPORTED)
    message(FATAL_ERROR "LTO unavailable: ${IPO_ERROR}")
  endif()
  set_property(TARGET ex369 PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(EX369_PGO STREQUAL "gen")
  target_compile_options(ex369 PUBLIC -fprofile-generate=${EX369_PGO_DIR})
  target_link_options(ex369 PUBLIC -fprofile-generate=${EX369_PGO_DIR})
elseif(EX369_PGO STREQUAL "use")
  target_compile_options(ex369 PUBLIC
      -fprofile-use=${EX369_PGO_DIR} -fprofile-correction -Wno-missing-profile)
  target_link_options(ex369 PUBLIC -fprofile-use=${EX369_PGO_DIR} -fprofile-correction)
endif()

add_executable(kh71 src/cli/main.cpp)
target_link_libraries(kh71 PRIVATE ex369)
target_compile_definitions(kh71 PRIVATE EX369_VERSION="${PROJECT_VERSION}")

# Drop a link to the binary at the project root, so ./kh71 works from there the
# way ./keyhunt does - the build tree is not where anyone wants to type paths.
if(EX369_ROOT_LINK)
  add_custom_command(TARGET kh71 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E create_symlink
          $<TARGET_FILE:kh71> ${CMAKE_SOURCE_DIR}/kh71
    COMMENT "linking ./kh71 -> $<TARGET_FILE:kh71>")
endif()

add_executable(microbench bench/microbench.cpp)
target_link_libraries(microbench PRIVATE ex369)
add_executable(hash_diagnostics bench/hash_diagnostics.cpp)
target_link_libraries(hash_diagnostics PRIVATE ex369)

enable_testing()
add_subdirectory(tests)
