# BLOC V1 — Implementation Plan

Plan revision 3 · 2026-10-03 · for `docs/BLOC_SPEC.md` revision 4

Revision 3 adds: CMake as the only build system and consumption via `FetchContent` (R-10, section 3.8, smoke tests FC-01..10 in section 9.8), and a multi-compiler, multi-architecture CI with emulated test execution (sections 3.6, 9.3, 9.6, 9.7, 10, Appendix A).

This plan tells an implementing agent exactly what to build, in which order, and how to prove it is correct. It is written to be followed step by step without further design work. The specification `docs/BLOC_SPEC.md` is the source of truth for behaviour; this plan adds structure, tooling, test requirements and acceptance criteria.

---

## 0. How to use this plan

1. Read `docs/BLOC_SPEC.md` completely before writing code. Then read this plan completely.
2. Work through the phases in section 12 in order. Do not start a phase before the previous one meets its Definition of Done.
3. Each phase implements code **and** its tests together. Coverage must be 100 % for all code that exists at the end of every phase, not only at the end of the project.
4. Commit once per phase (section 12 gives the message). Never commit with failing tests, warnings or coverage below 100 %.
5. If the spec and this plan disagree, the spec wins. If the spec is ambiguous, follow section 1.3.

---

## 1. Ground rules (non-negotiable)

### 1.1 Product rules

| ID | Rule |
| --- | --- |
| R-01 | BLOC never allocates memory. No `malloc`, `calloc`, `realloc`, `free`, `aligned_alloc`, `memmove`, `alloca`, VLAs. |
| R-02 | `src/` (library sources and the public headers in `src/include/`) may include only `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`, `<string.h>`. Never `<stdlib.h>`, `<stdio.h>`, `<assert.h>`. |
| R-03 | The only C library symbols the compiled library may reference are `memcpy` and `memset`. With `BLOC_DEBUG = 0` it must not reference any compiler runtime helper (libgcc, compiler-rt) on any target in section 3.6. With `BLOC_DEBUG = 1` it may additionally reference the target's unsigned division and modulo helpers (spec section 2). |
| R-04 | Pure C11 (`-std=c11`), no compiler extensions except `__builtin_trap` inside the guarded default of `BLOC_PLATFORM_ASSERT`. Compiler- or target-specific code (inline assembly, intrinsics) is allowed only in `test/` and in configuration headers. |
| R-05 | No recursion, no floating point, no function-local `static` variables, no global mutable state in `src/`. |
| R-06 | No `static inline` functions in public headers. All executable code lives in `src/bloc.c`, so coverage measures it. |
| R-07 | Do not add, remove or rename public API functions, types, macros or enum values beyond spec section 15 and the macros named in the spec. |
| R-08 | Every behaviour must be reachable from the public API or from a documented white-box test technique (section 6.4). No test-only code paths in `src/` (no `#ifdef TESTING`, no `#ifdef COVERAGE`). |
| R-09 | Minimal `.text` footprint (spec section 17): budgets on ARMv6-M and ARMv7-M, zero `.data`/`.bss`, no runtime helpers, and dead-strippable per function. Section 4.6 gives the implementation rules. |
| R-10 | CMake is the only build system. Other projects consume BLOC with CMake `FetchContent` (or `add_subdirectory`) and link `bloc::bloc`; doing so has no side effects on the consuming project. Section 3.8 gives the rules, section 9.8 the smoke tests. No Makefiles, Meson, Bazel or IDE project files are added; the scripts in `scripts/` drive CMake or compile the single translation unit directly for checks, they are not an alternative build. |

### 1.2 Quality rules

| ID | Rule |
| --- | --- |
| Q-01 | 100 % line, 100 % branch and 100 % function coverage of `src/bloc.c`, separately for **every** coverage configuration in section 6.2. |
| Q-02 | Coverage exclusion markers are forbidden: no `LCOV_EXCL_*`, `GCOVR_EXCL_*`, `// NOSONAR`-style or pragma-based exclusions. The only permitted gcovr option that hides branches is `--exclude-unreachable-branches`. |
| Q-03 | If a branch cannot be covered, the code is wrong: restructure it (remove a defensive branch that the spec does not require, merge conditions, reorder checks). Never lower thresholds. |
| Q-04 | Zero compiler warnings (`-Werror`) with every compiler and target in the compiler matrix (section 3.6), at the flags in section 3.3. |
| Q-05 | All tests pass under AddressSanitizer + UndefinedBehaviorSanitizer with both GCC and Clang, and the thread stress test passes under ThreadSanitizer. |
| Q-06 | Tests never hard-code layout numbers (4, 12, 16 …). They derive expected values from the public macros and the formulas in the spec, so the same test is valid in every configuration. |
| Q-07 | The footprint gates of section 9.5 pass: spec budgets are met, and the regression baseline (`scripts/size_baseline.txt`) never grows without a justified update in the same commit. |
| Q-08 | Many compilers, many architectures, many tests: the full unit test suite runs and passes on every tier-1 host compiler (oldest supported, middle and newest GCC and Clang) and on every tier-1E emulated target (32- and 64-bit, little- and big-endian) of section 3.6, not only on the developer's machine. Priorities, highest first: bare-metal embedded targets (tier 2), little- and big-endian targets with real test execution (tier 1E), Linux hosts (tier 1). macOS and Windows are non-goals (section 3.6). |
| Q-09 | The `FetchContent` smoke tests of section 9.8 pass. A change that breaks consumption by another CMake project is a defect even if all unit tests pass. |

### 1.3 Handling ambiguity

If something is unclear or the spec appears contradictory:

1. Do not invent new behaviour and do not edit `docs/BLOC_SPEC.md`.
2. Append an entry to `docs/OPEN_QUESTIONS.md` (create it if missing) with: spec section, the question, the interpretation you chose, and the test IDs affected.
3. Choose the interpretation that is closest to the spec text and most conservative (fewer side effects, earlier failure, no new API).
4. Mention the entry in the phase's commit message.

---

## 2. Deliverables and repository layout

```text
bloc/
├── CMakeLists.txt                 top-level build: library target, options, consumption rules (section 3.8)
├── CMakePresets.json              one configure preset per configuration (section 6)
├── README.md                      overview, quick start, FetchContent usage, build/test instructions (phase 7)
├── LICENSE                        existing, MIT
├── .clang-format                  formatting rules (section 3.7)
├── .github/
│   ├── workflows/ci.yml           CI pipeline: lint, gates, matrix, ci-ok (section 10)
│   └── actions/target/action.yml  composite action: install, build and test one target (section 10.3)
├── cmake/toolchains/              toolchain files for the tier-1E emulated targets (section 3.6)
├── docs/
│   ├── BLOC_SPEC.md               specification (existing)
│   ├── IMPLEMENTATION_PLAN.md     this file
│   └── OPEN_QUESTIONS.md          only if needed (section 1.3)
├── src/                           the library: the only directory a consumer needs (section 3.8)
│   ├── CMakeLists.txt             library target bloc / bloc::bloc, no project()
│   ├── bloc.c                     the whole implementation
│   └── include/
│       ├── bloc.h                 public API, types, layout macros
│       └── bloc_opt.h             configuration defaults and compile-time validation
├── examples/
│   ├── basic.c                    alloc / append / prepend / release example
│   └── bloc_opts_example.h        example project configuration header
├── scripts/
│   ├── coverage.sh                build + test + gcovr gate for every coverage config
│   ├── sanitize.sh                ASan/UBSan run over every coverage config
│   ├── check_no_heap.sh           symbol check for R-01..R-03 (host builds)
│   ├── cross_check.sh             cross-compile matrix and symbol check (section 9.4)
│   ├── check_size.sh              footprint report and gates (section 9.5)
│   ├── size_baseline.txt          committed footprint regression baseline
│   ├── fetchcontent_smoke.sh      FetchContent consumer smoke tests (section 9.8)
│   ├── run_all.sh                 everything CI does, locally
│   └── ci/
│       ├── target_table.sh        single source of truth: target name → compiler, flags, emulator, configs
│       ├── build_one.sh           build (and test) one target × configuration × build type
│       └── apt_install.sh         apt-get with retries, used by every CI job
└── test/
    ├── CMakeLists.txt
    ├── configs/                   one header per test configuration (section 6)
    ├── support/                   test support library (section 5)
    ├── compile_fail/              sources that must fail to compile (section 8.1)
    ├── target/                    compile-only static layout checks for cross targets (XC-05)
    ├── size/                      minimal link programs for dead-strip checks (FP-05)
    ├── fetchcontent_smoke/        stand-alone consumer project (section 9.8), never added by test/CMakeLists.txt
    │   ├── CMakeLists.txt         consumes BLOC exactly as the README shows; hygiene assertions
    │   ├── main.c                 uses the API and cross-checks consumer vs. library layout
    │   ├── config/smoke_opts.h    consumer configuration header (FC-03)
    │   └── toolchain-cortex-m0plus.cmake   bare-metal consumer (FC-08)
    ├── test_platform.c            target property self-check for emulated targets (EM-04)
    └── test_*.c                   Unity test files (section 8)
```

---

## 3. Toolchain and build system

### 3.1 Tools

| Tool | Version | Use |
| --- | --- | --- |
| CMake | ≥ 3.20 (devcontainer: 4.2); the floor 3.20 is tested in CI (FC-07) | The only build system (R-10), CTest |
| Ninja | any (devcontainer: 1.13) | CMake generator |
| GCC | ≥ 11 (devcontainer: 15.2) | Host compiler, coverage (`gcov`), sanitizers |
| Clang | ≥ 14 (devcontainer: 21.1) | Host compiler, sanitizers; ARM and RISC-V cross compiler |
| `arm-none-eabi-gcc` + newlib | devcontainer: 14.2 | ARM cross compiler, footprint reference compiler (spec section 17) |
| `riscv64-unknown-elf-gcc` + picolibc | devcontainer: 14.2 | RISC-V cross compiler (RV32 and RV64) |
| `avr-gcc` + avr-libc | devcontainer: 14.3 | 8-bit AVR cross compiler (16-bit `int` and `size_t`) |
| gcovr | ≥ 7.0 (devcontainer: 7.2) | Coverage report and gate (`--fail-under-function` needs ≥ 7) |
| Unity | v2.6.1 (tag), fetched with CMake `FetchContent` | Unit test framework |
| binutils `nm`, `size` (host and `<triple>-` prefixed) | any | Symbol and footprint checks |
| Linux cross GCCs: `gcc-aarch64-linux-gnu`, `gcc-arm-linux-gnueabihf`, `gcc-riscv64-linux-gnu`, `gcc-powerpc-linux-gnu`, `gcc-s390x-linux-gnu` | distro default | Tier-1E emulated targets (section 3.6) |
| `qemu-user` | distro default (Ubuntu 26.04 has no `qemu-user-static`; `qemu-user` ships statically linked binaries) | Runs the tier-1E test binaries (user-mode emulation) |
| `gcc-multilib` | distro default | 32-bit host build (CC-02). Conflicts with every Linux cross GCC (checked on Ubuntu 26.04), so it is installed only in the CI `host -m32` cell, never in the devcontainer. |
| Python `venv` + `pip install cmake==3.20.*` | – | CMake floor check (FC-07), CI only |

The devcontainer (`.devcontainer/Dockerfile`, Ubuntu 26.04) is the reference environment. Footprint baselines are only valid for the compiler versions recorded in them (section 9.5). Bare-metal cross targets (tier 2) are compile-, link- and symbol-checked but do not run tests. The tier-1E emulated targets do run the full test suite; their packages (Linux cross GCCs with their `libc6-dev-<arch>-cross`, and `qemu-user`) are installed in the devcontainer and in CI. `gcc-multilib` cannot be co-installed with the cross GCCs, so CC-02 runs in CI only. Where a tool is missing locally, `scripts/run_all.sh` prints a visible `SKIP` line for each affected target instead of failing.

Unity is fetched at configure time:

```cmake
include(FetchContent)
FetchContent_Declare(unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG        v2.6.1
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(unity)
```

### 3.2 CMake options

Options available in every build, including when BLOC is consumed by another project:

| Option | Default | Meaning |
| --- | --- | --- |
| `BLOC_BUILD_TESTS` | `ON` when top-level project, else `OFF` | Build tests and register them with CTest. Setting it `ON` while BLOC is not the top-level project is a configure error (`BLOC tests can only be built when BLOC is the top-level project`). |
| `BLOC_BUILD_EXAMPLES` | `ON` when top-level project, else `OFF` | Build `examples/` |
| `BLOC_WERROR` | `ON` when top-level project, else `OFF` | Add `-Werror` to the `bloc` target (PRIVATE). Off for consumers, so a newer consumer compiler cannot break their build with a new warning. |
| `BLOC_CONFIG_HEADER` | empty | Project configuration header (spec section 6), e.g. `bloc_opts.h`. If set, `BLOC_CONFIG_HEADER="<value>"` becomes a PUBLIC compile definition of `bloc` (section 3.8, CM-06). |
| `BLOC_CONFIG_DIRS` | empty | Absolute include directories (`;`-list) that contain `BLOC_CONFIG_HEADER`; PUBLIC include directories of `bloc`. A relative path is a configure error. |

Options that exist **only** when BLOC is the top-level project (they are not even defined as cache entries otherwise, FC-02):

| Option | Default | Meaning |
| --- | --- | --- |
| `BLOC_TEST_CONFIG` | `default` | One of `default`, `debug`, `nochecks`, `wide`, `noalign`, `bigalign`, `pthread` |
| `BLOC_COVERAGE` | `OFF` | Adds `-O0 -g --coverage` to library and tests (GCC only) |
| `BLOC_SANITIZE` | empty | Comma list passed to `-fsanitize=`, e.g. `address,undefined` or `thread` |
| `BLOC_LTO` | `OFF` | `INTERPROCEDURAL_OPTIMIZATION` on library, test support and tests, plus `-O3 -fstrict-aliasing` (LTO-01). Uses `check_ipo_supported()`; CMake then picks `gcc-ar`/`llvm-ar` itself. |

The host compiler is chosen with the preset (`CMAKE_C_COMPILER`), not with an option. Preset names are `<compiler>-<cfg>` (Debug, `-O0`), `<compiler>-<cfg>-rel` (Release, `-O2`), `<compiler>-<cfg>-size` (MinSizeRel, `-Os`), `cov-<cfg>` (GCC coverage), `san-<compiler>-<cfg>` (sanitizers) and `lto-<compiler>-<cfg>` (LTO), with `<compiler>` ∈ {`gcc`, `clang`}. Build directories are `build/<preset>`. CI does not use presets; it calls `scripts/ci/build_one.sh`, which passes the same cache variables (section 10.4).

`BLOC_TEST_CONFIG` selects `test/configs/cfg_<name>.h`. For every value except `default`, the top-level build sets `BLOC_CONFIG_HEADER=cfg_<name>.h` and `BLOC_CONFIG_DIRS=<abs>/test/configs;<abs>/test/support` before the library target is created. The test configurations therefore use **the same mechanism a consumer uses** (section 3.8), and because the definition and include directories are PUBLIC, the library and every test are always compiled with the same configuration. For `default`, `BLOC_CONFIG_HEADER` is not defined at all; this proves the library builds with zero configuration. Setting both `BLOC_TEST_CONFIG` (≠ `default`) and `BLOC_CONFIG_HEADER` is a configure error.

Targets:

- `bloc` — the only target defined when BLOC is consumed: static library from `src/bloc.c`, defined in `src/CMakeLists.txt`, public include dir `src/include/`, alias `bloc::bloc`. Rules in section 3.8.
- `bloc_test_support` — static library from `test/support/*.c` (tests only).
- one executable per `test/test_*.c`, each linking `bloc::bloc`, `bloc_test_support` and `unity`, each registered with `add_test` (tests only).
- `compile_fail_*` targets (section 8.1, tests only).

### 3.3 Compiler flags

Library (`bloc`), both compilers:

```text
-std=c11 -Wall -Wextra -Wpedantic -Werror
-Wconversion -Wsign-conversion -Wshadow -Wundef -Wvla -Wpointer-arith
-Wstrict-prototypes -Wmissing-prototypes -Wcast-align -fno-common
```

GCC additionally: `-Wcast-align=strict` (replaces `-Wcast-align`). Release builds: `-O2`. Coverage builds: `-O0 -g --coverage`.

Tests: `-std=c11 -Wall -Wextra -Wpedantic -Werror` (no `-Wconversion`). Unity itself is compiled without `-Werror`.

Cross targets (section 3.6) use the library flags above plus the target flags, `-Os -ffunction-sections -fdata-sections`, and `-ffreestanding` only for the static layout checks in `test/target/`. GCC targets also use `-Wcast-align=strict`. Clang cross builds get the C library headers through `-isystem` (newlib: `/usr/lib/arm-none-eabi/include`; picolibc: `/usr/lib/picolibc/riscv64-unknown-elf/include`), because the GNU toolchains report no usable `-print-sysroot`.

`-Wcast-qual` is intentionally not enabled: `bloc_data()` is the single sanctioned place that removes `const` (spec section 10); mark it with a comment.

### 3.4 Casting rules that keep `-Wcast-align=strict` quiet

- Byte pointer to handle: `(struct bloc_handle *)(void *)p`.
- Handle to byte pointer: `(uint8_t *)(void *)h`.
- Address comparisons for debug checks: convert to `uintptr_t` first.

### 3.5 Portability rules

Some targets in the matrix differ from a 64-bit host, and the code must be correct on all of them:

- `int` may be 16 bits (AVR), so `uint16_t` operands may not promote to `int`. Never rely on promotion for correctness; cast explicitly.
- `size_t` and pointers may be 16 bits (AVR). Configurations with `BLOC_SIZE_T` wider than `size_t` are rejected at compile time (XC-04).
- There may be no hardware divider (ARMv6-M, AVR, RV32I/RV32E) and no unaligned access (ARMv6-M).
- Literal pools on ARM are part of `.text`.

### 3.6 Compiler matrix

| Tier | Compiler | Targets / flags | What runs |
| --- | --- | --- | --- |
| 1 Host (reference) | GCC 15, Clang 21 (`ubuntu:26.04`, = devcontainer) | native x86-64 | Full build, all unit tests, compile-fail tests, in all 7 configurations at Debug, plus Release/MinSizeRel (CC-01, CC-03); sanitizers; GCC coverage; LTO (LTO-01); no-heap; FetchContent smoke |
| 1 Host (older) | GCC 13, Clang 18 (`ubuntu:24.04`); GCC 11, Clang 14 (`ubuntu:22.04`, oldest supported) | native x86-64 | Same build and test scope as the reference row, no coverage or sanitizers (CC-04) |
| 1 Host (32-bit) | GCC 15 with `-m32` (`gcc-multilib`) | i386 | All 7 configurations at Debug, `default` at Release (CC-02) |
| 1E Emulated | Linux cross GCC, static linking, tests run under `qemu-<arch>` (user mode) | `aarch64` (64-bit LE); `armhf` (`arm-linux-gnueabihf`, ARMv7 32-bit LE); `riscv64` (64-bit LE); `powerpc` (32-bit **big-endian**); `s390x` (64-bit **big-endian**) | All unit tests in all 7 configurations at Debug, `default` and `nochecks` at MinSizeRel; symbol check (EM-01..04) |
| 2 Cross | `arm-none-eabi-gcc` | ARMv6-M `-mcpu=cortex-m0plus -mthumb`; ARMv7-M `-mcpu=cortex-m3 -mthumb`; ARMv7E-M `-mcpu=cortex-m4 -mthumb -mfloat-abi=soft`; ARMv7-A `-mcpu=cortex-a7 -mthumb -mfloat-abi=soft`; ARMv7-R `-mcpu=cortex-r5 -mthumb -mfloat-abi=soft` | Library compile, static layout checks, symbol check, footprint, dead-strip link |
| 2 Cross | Clang | `--target=thumbv6m-none-eabi -mcpu=cortex-m0plus`; `--target=thumbv7m-none-eabi -mcpu=cortex-m3`; `--target=riscv32-unknown-elf -march=rv32imac -mabi=ilp32` | Library compile, static layout checks, symbol check, footprint |
| 2 Cross | `riscv64-unknown-elf-gcc` | RV32IMAC `-march=rv32imac -mabi=ilp32`; RV32I `-march=rv32i -mabi=ilp32` (no divider); RV64IMAC `-march=rv64imac -mabi=lp64` | Library compile, static layout checks, symbol check, footprint |
| 2 Cross | `avr-gcc` | `-mmcu=atmega328p` (16-bit `int`, `size_t` and pointers) | Library compile, static layout checks, symbol check, footprint |
| 3 Target execution | any tier-2 compiler + QEMU | — | Optional and not required. Registered only if `qemu-system-arm` is found: runs the unit tests on `mps2-an385` (Cortex-M3) with semihosting. Not part of any gate. |

Why so many rows: each one catches a class of bugs the others cannot. Older compilers catch reliance on recent C11 support and different warning sets. `-m32` and `armhf`/`powerpc` give 32-bit pointers and `size_t` with real test execution. `powerpc` and `s390x` are big-endian, so any test or helper that silently assumes byte order fails there. The bare-metal tier 2 adds 8- and 16-bit `int`, no hardware divider and strict alignment, and is where size is measured.

Tier-1E details: each target has a CMake toolchain file `cmake/toolchains/linux-<arch>.cmake` that sets `CMAKE_SYSTEM_NAME Linux`, `CMAKE_SYSTEM_PROCESSOR`, `CMAKE_C_COMPILER <triple>-gcc`, `CMAKE_EXE_LINKER_FLAGS_INIT -static` and `CMAKE_CROSSCOMPILING_EMULATOR qemu-<arch>` (`qemu-aarch64`, `qemu-arm`, `qemu-riscv64`, `qemu-ppc`, `qemu-s390x`; these binaries are statically linked, so the name has no `-static` suffix on Ubuntu 26.04). With static linking and an explicit emulator, `ctest` runs the binaries without a sysroot and without `binfmt_misc`, which a CI container cannot register. If a package name differs on the CI image, keep the target's properties (word size, endianness) and record the substitution in `docs/OPEN_QUESTIONS.md`.

**Non-goals.** macOS (AppleClang, Mach-O) and Windows are not supported or tested in V1 and have no priority. They may be considered after every other item in this plan is done. Nothing in `src/` may prevent such a port, but no job, test or script is spent on it.

Tier-2 configurations: every test configuration except `pthread`. AVR also excludes `wide`, which must fail there with the expected message (XC-04). Test configuration headers, and the support headers they include, must therefore compile freestanding: they may only include the headers allowed by R-02.

### 3.7 Style

- `.clang-format` based on LLVM, `IndentWidth: 4`, `ColumnLimit: 100`, `BreakBeforeBraces: Linux`.
- Public symbols prefixed `bloc_` / `BLOC_`; internal `static` helpers prefixed `bloc_i_`.
- Every public function has a Doxygen comment in `bloc.h`: brief, parameters, return values (every status code it can return), thread-safety note.
- Use `0u`, `1u` for unsigned literals; explicit casts when narrowing (`b->offset = (bloc_size_t)(b->offset - n);`).

### 3.8 CMake project rules and consumption by other projects (R-10)

BLOC is meant to be pulled into firmware and host projects with a few lines of CMake. The library and its build description therefore live together in `src/`, separate from everything else:

- `src/CMakeLists.txt` is the **library**: it defines the target `bloc` (STATIC) with the alias `bloc::bloc` and the `BLOC_*` configuration entries, and nothing else. It calls `cmake_minimum_required` but no `project()`, adds no tests, fetches nothing and sets no global state. This file is what consumers add.
- The top-level `CMakeLists.txt` is the **development project** for BLOC's own developers and CI: `project(bloc VERSION X.Y.Z)`, `add_subdirectory(src)`, then the test-only options (`BLOC_TEST_CONFIG`, `BLOC_COVERAGE`, `BLOC_SANITIZE`, `BLOC_LTO`), instrumentation, tests and examples. It sets `BLOC_WERROR` to `ON` as a normal variable before `add_subdirectory(src)`, so warnings are errors in BLOC's own build while consumers get the option's default `OFF`. If a consumer adds the repository root instead of `src/`, the top-level file detects that it is not the top-level project and only adds `src/`.

Because the consumable unit declares no `project()`, adding it creates neither `bloc_*` cache entries nor CMake's `CMAKE_PROJECT_VERSION*` entries in the consumer (OQ-001).

#### Supported consumption

```cmake
# >>> bloc-fetchcontent  (this block is kept identical in README.md and test/fetchcontent_smoke/CMakeLists.txt, FC-10)
include(FetchContent)
FetchContent_Declare(bloc
  GIT_REPOSITORY https://github.com/Andste82/bloc.git
  GIT_TAG        ${BLOC_GIT_TAG}   # a release tag such as v1.0.0, or a full commit hash
  GIT_SHALLOW    ${BLOC_GIT_SHALLOW}
  SOURCE_SUBDIR  src)               # the library only: no project, no tests
# optional project configuration (spec section 6):
# set(BLOC_CONFIG_HEADER "bloc_opts.h")
# set(BLOC_CONFIG_DIRS   "${CMAKE_CURRENT_SOURCE_DIR}/config")
FetchContent_MakeAvailable(bloc)
# <<< bloc-fetchcontent

target_link_libraries(my_app PRIVATE bloc::bloc)
```

In the README the two variables are replaced by literal values (`v1.0.0`, `TRUE`); the FC-10 check compares the blocks after that substitution. `SOURCE_SUBDIR` needs CMake 3.18, below the floor of 3.20. `add_subdirectory(path/to/bloc/src bloc)` works the same way and is documented in one sentence. To test a local checkout without network access, consumers and CI use CMake's standard override `-DFETCHCONTENT_SOURCE_DIR_BLOC=/path/to/bloc`; the consumer's `CMakeLists.txt` stays unchanged.

The files `src/CMakeLists.txt` and `CMakeLists.txt` are the reference for the details; the rules below are what they must keep true.

#### Rules

| ID | Rule |
| --- | --- |
| CM-01 | The consumer-visible result of `FetchContent_MakeAvailable(bloc)` (with `SOURCE_SUBDIR src`) with default options is exactly one buildable target, `bloc` (STATIC), with the alias `bloc::bloc`. No other targets, no subdirectories, no tests, no Unity download. Consumers link `bloc::bloc`. |
| CM-02 | Usage requirements of `bloc` are exactly: the `src/include/` directory (`BUILD_INTERFACE`), the compile feature `c_std_11`, and, if configured, the `BLOC_CONFIG_HEADER` definition plus `BLOC_CONFIG_DIRS`. `INTERFACE_LINK_LIBRARIES` and `INTERFACE_COMPILE_OPTIONS` are empty: warning, coverage, sanitizer and LTO flags are PRIVATE or test-only and never reach the consumer. |
| CM-03 | No global side effects. BLOC's CMake code never calls `add_compile_options`, `add_link_options`, `add_definitions`, `include_directories`, `link_libraries` or `include(CTest)`, never sets `CMAKE_C_FLAGS*`, `CMAKE_C_STANDARD`, `CMAKE_BUILD_TYPE`, `CMAKE_*_OUTPUT_DIRECTORY` or `CMAKE_POSITION_INDEPENDENT_CODE`, and never writes `PARENT_SCOPE` variables. All flags are set per target. |
| CM-04 | Namespace hygiene. Cache entries created by `src/CMakeLists.txt` start with `BLOC_`; it calls no `project()`, so there are no `bloc_*` or `CMAKE_PROJECT_VERSION*` entries. Test-only options (`BLOC_TEST_CONFIG`, `BLOC_COVERAGE`, `BLOC_SANITIZE`, `BLOC_LTO`) exist only in a top-level build. |
| CM-05 | Location independence. Inside BLOC's CMake files, paths are built from `CMAKE_CURRENT_SOURCE_DIR` or `CMAKE_CURRENT_BINARY_DIR` (`src/CMakeLists.txt` uses only these), or `bloc_SOURCE_DIR` in the top-level file; `CMAKE_SOURCE_DIR` and `CMAKE_BINARY_DIR` appear only in the top-level detection. |
| CM-06 | Configuration consistency (no ODR split). The project configuration header is applied to the `bloc` target as a PUBLIC definition, never to the consumer's target alone, so the library and every consumer translation unit see the same `struct bloc_handle`, `struct bloc_pool` and layout macros. A consumer that defines `BLOC_CONFIG_HEADER` only on its own target gets a silently mismatched layout; FC-04 proves the smoke test detects exactly that. |
| CM-07 | The library compiles as C11 regardless of the consumer's `CMAKE_C_STANDARD` (target property `C_STANDARD 11`). The public headers must also compile in consumer translation units built as C11, C17 and C23 with strict warnings (FC-05). |
| CM-08 | Portable configure. No `try_run`, no `check_*_runs`, no `find_package`, no `CMAKE_BUILD_TYPE` checks (multi-config generators must work), no network access unless `BLOC_BUILD_TESTS` is on. Configuring with a bare-metal toolchain file (`CMAKE_SYSTEM_NAME Generic`, `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY`) and with compilers other than GCC/Clang must work; unknown compilers simply get no warning flags. |
| CM-09 | Versioning. `project(bloc VERSION X.Y.Z)` in the top-level `CMakeLists.txt` and `BLOC_VERSION_MAJOR/MINOR/PATCH` in `bloc.h` match the release tag `vX.Y.Z`. Consumers are told to pin a tag or a full commit hash, never a branch. |
| CM-10 | Out of scope for V1: `install()`, `export()`, a package config for `find_package(bloc)`, and shared-library builds. `bloc` is always STATIC and ignores `BUILD_SHARED_LIBS`. If a consumer needs position-independent code, they set `CMAKE_POSITION_INDEPENDENT_CODE` in their own project, which reaches `bloc` like any other target. |

---

## 4. Code architecture

### 4.1 `src/include/bloc_opt.h`

Contents, in this order:

1. Include guard.
2. Default for every option in spec section 6, each wrapped in `#ifndef`. Defaults: `BLOC_BLOCK_ALIGNMENT 4`, `BLOC_PAYLOAD_ALIGNMENT 4`, `BLOC_SIZE_T uint16_t`, `BLOC_COUNT_T uint8_t`, `BLOC_REFCOUNT_T uint8_t`, `BLOC_THREAD_SAFE 0`, `BLOC_CHECKS 1`, `BLOC_DEBUG 0`, `BLOC_STATS 0`.
3. Default `BLOC_PLATFORM_ASSERT(msg)` exactly as in spec section 6 (trap, no libc).
4. Preprocessor validation with `#error` and the exact messages from spec section 6:
   - `#if BLOC_DEBUG && !BLOC_CHECKS` → `#error "BLOC_DEBUG requires BLOC_CHECKS"`.
   - `#if BLOC_THREAD_SAFE && !(defined(BLOC_DECL_PROTECT) && defined(BLOC_PROTECT) && defined(BLOC_UNPROTECT))` → `#error "BLOC_THREAD_SAFE requires BLOC_DECL_PROTECT, BLOC_PROTECT and BLOC_UNPROTECT"`.
5. `_Static_assert`s for the other rules in spec section 6 with the exact messages. Use a helper `#define BLOC_IS_POW2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))`.

### 4.2 `src/include/bloc.h`

Contents, in this order:

1. Include guard, `extern "C"` block for C++ consumers.
2. `#include <stddef.h>`, `<stdint.h>`, `<stdbool.h>`.
3. `#ifdef BLOC_CONFIG_HEADER` → `#include BLOC_CONFIG_HEADER`.
4. `#include "bloc_opt.h"`.
5. Version macros: `BLOC_VERSION_MAJOR 1`, `BLOC_VERSION_MINOR 0`, `BLOC_VERSION_PATCH 0`.
6. Typedefs `bloc_size_t`, `bloc_count_t`, `bloc_refcount_t`; `*_MAX` constants (spec section 6).
7. `bloc_status_t` enum (spec section 13, exact order and values).
8. `struct bloc_handle`, `struct bloc_pool` (spec sections 5 and 7, exact field order), handle/pool typedefs.
9. `bloc_pool_stats_t` under `#if BLOC_STATS`.
10. Layout macros: `BLOC_MAX2`, `BLOC_MAX3`, `BLOC_STORAGE_ALIGNMENT`, `BLOC_ALIGN_UP`, `BLOC_HEADER_SIZE`, `BLOC_BLOCK_STRIDE`, `BLOC_POOL_SIZE`, `BLOC_POOL_STORAGE`, `BLOC_ELEMENT_SIZE_MAX` (spec section 4). `BLOC_MAX3` casts operands to `size_t`. All must be integer constant expressions.
11. Function prototypes (spec section 15) with Doxygen comments.

### 4.3 `src/bloc.c` internal macros

```c
#include <string.h>
#include "bloc.h"

#if BLOC_DEBUG
#  define BLOC_I_FAIL(msg)              BLOC_PLATFORM_ASSERT(msg)
#else
#  define BLOC_I_FAIL(msg)              ((void)0)
#endif

/* Always-compiled check with assert-in-debug and early return. */
#define BLOC_I_REQUIRE(cond, msg, ret) \
    do { if (!(cond)) { BLOC_I_FAIL(msg); return (ret); } } while (0)

/* Check compiled only with BLOC_CHECKS. */
#if BLOC_CHECKS
#  define BLOC_I_CHECK(cond, msg, ret)  BLOC_I_REQUIRE(cond, msg, ret)
#else
#  define BLOC_I_CHECK(cond, msg, ret)  ((void)0)
#endif

#if BLOC_THREAD_SAFE
#  define BLOC_I_LOCK_DECL(l)           BLOC_DECL_PROTECT(l)
#  define BLOC_I_LOCK(l)                BLOC_PROTECT(l)
#  define BLOC_I_UNLOCK(l)              BLOC_UNPROTECT(l)
#else
#  define BLOC_I_LOCK_DECL(l)           /* nothing */
#  define BLOC_I_LOCK(l)                ((void)0)
#  define BLOC_I_UNLOCK(l)              ((void)0)
#endif
```

Write `BLOC_I_LOCK_DECL(lev);` as a statement; with thread safety off it becomes an empty statement.

Assertion messages are short string literals prefixed with the function name, e.g. `"bloc_append: n exceeds tailroom"`. Tests match only the presence of an assertion, never the text, except where section 8 says otherwise.

### 4.4 `src/bloc.c` internal helpers

| Helper | Compiled when | Purpose |
| --- | --- | --- |
| `static uint8_t *bloc_i_data_start(const struct bloc_handle *b)` | always | `(uint8_t *)(uintptr_t)b + BLOC_HEADER_SIZE` |
| `static bool bloc_i_addr_valid(const struct bloc_handle *b)` | `BLOC_DEBUG` | Handle validity steps 2–5 (spec section 13), one `if` per step. Step 5 is the only `%` in the library (allowed under R-03 in debug builds only). |
| `static bool bloc_i_handle_valid(const struct bloc_handle *b)` | `BLOC_DEBUG` | Step 1 (`refcount != 0`) then `bloc_i_addr_valid` |
| `static bool bloc_i_overlaps(const void *ext, size_t n, const uint8_t *dst)` | `BLOC_DEBUG` | `n != 0 && ext < dst + n && dst < ext + n`, compared as `uintptr_t` |

Handle-taking functions other than retain/release start with two stages, matching the check order of spec section 13:

```c
/* stage 1: NULL checks for every pointer argument, in parameter order */
BLOC_I_CHECK(dst != NULL, "fn: dst is NULL", ret_invalid);
BLOC_I_CHECK(src != NULL, "fn: src is NULL", ret_invalid);
/* stage 2: debug validity for every handle argument, in parameter order */
#if BLOC_DEBUG
BLOC_I_REQUIRE(bloc_i_handle_valid(dst), "fn: invalid dst handle", ret_invalid);
BLOC_I_REQUIRE(bloc_i_handle_valid(src), "fn: invalid src handle", ret_invalid);
#endif
```

where `ret_invalid` is `BLOC_INVALID`, `NULL` or `0` depending on the return type. In the sketches below, "pointer checks" means exactly these two stages for the function's arguments.

### 4.5 Function sketches

These sketches fix the check order of spec section 13 and the lock placement of spec section 12. They are guidance, not literal code; the spec decides behaviour.

**`bloc_pool_init`** (all checks always compiled, via `BLOC_I_REQUIRE`)

```text
REQUIRE pool && storage && element_count != 0 && element_size != 0      → BLOC_INVALID
REQUIRE (size_t)element_size <= BLOC_ELEMENT_SIZE_MAX                  → BLOC_INVALID
REQUIRE ((uintptr_t)storage & (BLOC_STORAGE_ALIGNMENT - 1)) == 0       → BLOC_ALIGNMENT
stride = BLOC_BLOCK_STRIDE(element_size)
rem = storage_size                       -- no division (R-03), no overflow
for i in 0 .. element_count-1:
    REQUIRE rem >= stride                                              → BLOC_BOUNDS
    rem -= stride
-- only now write anything --
for i in 0 .. element_count-2: block(i).refcount = 0; block(i).link.next_free = block(i+1)
block(last).refcount = 0; block(last).link.next_free = NULL
pool fields: storage, free_head = block(0), element_size, block_stride, element_count,
             active_count = 0, stats = 0
return BLOC_OK
```

**`bloc_pool_deinit`**

```text
CHECK pool != NULL                                  → BLOC_INVALID
LOCK
if storage == NULL:   UNLOCK; FAIL; return BLOC_INVALID
if active_count != 0: UNLOCK; return BLOC_BUSY      (valid runtime condition: no assert)
storage = NULL; free_head = NULL; element_count = 0; active_count = 0
UNLOCK; return BLOC_OK
```

**`bloc_pool_free_count`**: `CHECK pool != NULL → 0`; LOCK; `r = element_count - active_count`; UNLOCK; return r.

**`bloc_pool_get_stats`** (`BLOC_STATS`): `CHECK pool && out → BLOC_INVALID`; LOCK; if `storage == NULL` → UNLOCK, FAIL, `BLOC_INVALID`; copy both counters; UNLOCK; `BLOC_OK`.

**`bloc_alloc`**

```text
CHECK pool != NULL                                  → NULL
CHECK pool->storage != NULL                         → NULL
off = BLOC_ALIGN_UP(headroom, BLOC_PAYLOAD_ALIGNMENT)          (size_t)
if off > element_size: return NULL                  (no assert, no lock, no stat)
LOCK
b = free_head
if b == NULL: [STATS: if alloc_failures < BLOC_COUNT_MAX then alloc_failures++]
              UNLOCK; return NULL
free_head = b->link.next_free
b->link.pool = pool; b->refcount = 1; b->len = 0; b->offset = (bloc_size_t)off
active_count++
[STATS: if active_count > high_water then high_water = active_count]
[DEBUG: inv_ok = active_count <= element_count]
UNLOCK
[DEBUG: if !inv_ok then FAIL("bloc_alloc: active_count invariant")]
return b
```

With `BLOC_CHECKS = 0` an uninitialized pool reaches the empty-list branch and returns `NULL`; that is acceptable.

**`bloc_calloc`**: `b = bloc_alloc(pool, headroom)`; if `b != NULL` then `memset(bloc_i_data_start(b), 0, pool->element_size)`; return `b`.

**`bloc_retain`**

```text
CHECK b != NULL                                     → BLOC_INVALID
LOCK
if refcount == 0:          UNLOCK; FAIL; return BLOC_INVALID       (always compiled)
[DEBUG: if !bloc_i_addr_valid(b): UNLOCK; FAIL; return BLOC_INVALID]
if refcount == BLOC_REFCOUNT_MAX: UNLOCK; return BLOC_OVERFLOW     (always compiled, no assert)
refcount++
UNLOCK; return BLOC_OK
```

**`bloc_release`**

```text
if b == NULL: return BLOC_OK                        (always compiled, before lock)
LOCK
if refcount == 0:          UNLOCK; FAIL; return BLOC_INVALID
[DEBUG: if !bloc_i_addr_valid(b): UNLOCK; FAIL; return BLOC_INVALID]
if refcount > 1: refcount--; UNLOCK; return BLOC_OK
pool = b->link.pool                                 (read BEFORE writing next_free)
refcount = 0; b->link.next_free = pool->free_head; pool->free_head = b
active_count--
[DEBUG: inv_ok = active_count <= element_count]
UNLOCK
[DEBUG: if !inv_ok then FAIL]
return BLOC_OK
```

**Accessors**: pointer checks (`b`) → `NULL`/`0`; return the value. `bloc_tailroom` = `element_size - offset - len` using `b->link.pool->element_size`.

**`bloc_set_len`**: pointer checks (`b`); `CHECK (size_t)len <= element_size - offset → BLOC_BOUNDS`; `DEBUG: if refcount > 1 FAIL (continue)`; `len = len`.

**`bloc_add_header`**: pointer checks (`b`); `CHECK n <= offset → BOUNDS`; shared-mutation assert; `offset -= n; len += n`.

**`bloc_remove_header`**: pointer checks (`b`); `CHECK n <= len → BOUNDS`; shared-mutation assert; `offset += n; len -= n`.

**`bloc_copy_from`**: pointer checks (`dst`, `src`); `CHECK n <= element_size - offset → BOUNDS`; `DEBUG: REQUIRE !overlaps(src, n, data_start + offset) → INVALID`; shared-mutation assert; `memcpy`; `len = n`.

**`bloc_copy_to`**: pointer checks (`src`, `dst`); `CHECK pos <= len → BOUNDS`; `CHECK n <= len - pos → BOUNDS`; `memcpy(dst, data_start + offset + pos, n)`.

**`bloc_copy`**: pointer checks (`dst`, `src`); `if dst == src return BLOC_OK`; `CHECK src->len <= dst_element_size - dst->offset → BOUNDS`; shared-mutation assert on `dst`; `memcpy`; `dst->len = src->len`.

**`bloc_append`**: pointer checks (`dst`, `src`); `CHECK n <= src->len → BOUNDS`; `CHECK n <= tailroom(dst) → BOUNDS`; shared-mutation assert on `dst`; `memcpy(dst_payload_end, src_payload, n)`; `dst->len += n`.

**`bloc_append_data`**: pointer checks (`dst`, `src`); `CHECK n <= tailroom → BOUNDS`; `DEBUG overlap → INVALID`; shared-mutation assert; `memcpy`; `len += n`.

**`bloc_prepend`**: pointer checks (`dst`, `src`); `CHECK n <= src->len → BOUNDS`; `CHECK n <= dst->offset → BOUNDS`; shared-mutation assert; `memcpy(dst_payload - n, src_payload, n)`; `dst->offset -= n; dst->len += n`. For `src == dst` the source pointer must be computed **before** the offset changes.

**`bloc_prepend_data`**: as `bloc_prepend` with an external source (pointer checks on `dst`, `src`), plus the debug overlap check after the bounds checks.

### 4.6 Code-size rules (R-09)

These rules implement spec section 17. Their effect is measured (section 9.5), not assumed.

- Factor out repeated address arithmetic: `bloc_i_data_start`, plus `bloc_i_payload(b)` = data start + offset. Copy, append and prepend share one internal routine for the common "bounds known, `memcpy`, update `offset`/`len`" step where that measurably reduces size. Do not merge public functions into one dispatcher with a mode argument: that would defeat dead-stripping (FP-05).
- Keep error exits cheap: return status codes directly and avoid duplicated unlock/return sequences. A single exit label per protected section is acceptable if it reduces size.
- Avoid `size_t` arithmetic where `bloc_size_t` is provably sufficient: on ARM both are 32-bit, but on AVR `size_t` widening costs instructions. Overflow-safe subtraction-form checks (spec section 13) need no widening.
- Avoid loads of 64-bit constants and avoid `switch` statements that generate tables in `.rodata` (FP-03).
- Do not add `__attribute__((noinline/always_inline))` or other compiler pragmas in `src/` (R-04). Influence code size through structure only.
- Every structural change that changes the footprint updates `scripts/size_baseline.txt` in the same commit (FP-04), with the size delta per target in the commit message.

---

## 5. Test support library (`test/support/`)

All helpers are plain C, use only static memory and are used by every test file.

### 5.1 Guarded storage arena (`ts_arena.h/.c`)

- One static arena: `static _Alignas(64) uint8_t ts_arena_mem[4 * 1024 * 1024];` plus a bump index.
- `uint8_t *ts_storage(size_t size, size_t misalign)` returns a region of `size` bytes whose address is aligned to `BLOC_STORAGE_ALIGNMENT` and then offset by `misalign`. Each region is surrounded by 64-byte guard bands filled with `0xA5`. The region itself is filled with `0x5A`.
- `void ts_arena_reset(void)` in `setUp()`; `void ts_arena_check(void)` in `tearDown()` fails the test if any guard byte changed.
- Every test that creates a pool uses `ts_storage`. No test uses `malloc`.

### 5.2 Assertion hook (`ts_assert.h/.c`)

```c
void ts_assert_fail(const char *msg);   /* target of BLOC_PLATFORM_ASSERT in test configs */
extern unsigned    ts_assert_count;
extern unsigned    ts_assert_unexpected;
extern int         ts_assert_lock_depth;   /* lock depth when the last assert fired */
extern const char *ts_assert_last_msg;
```

- The hook records and **returns** (it does not abort), so the bail/continue semantics of spec section 13 are testable.
- `TS_EXPECT_ASSERT(stmt)`: resets the counter, marks an assertion as expected, runs `stmt`, then checks `ts_assert_count == 1` and `ts_assert_lock_depth == 0`.
- `TS_EXPECT_NO_ASSERT(stmt)`: checks `ts_assert_count == 0`.
- An assertion outside `TS_EXPECT_ASSERT` increments `ts_assert_unexpected`; `tearDown()` fails the test if it is non-zero.

### 5.3 Lock tracer (`ts_lock.h/.c`)

For configurations with `BLOC_THREAD_SAFE = 1` and counting macros:

```c
#define BLOC_DECL_PROTECT(lev)  int lev
#define BLOC_PROTECT(lev)       ((lev) = ts_lock_enter())
#define BLOC_UNPROTECT(lev)     ts_lock_exit(lev)
```

- `ts_lock_enter()` increments `ts_lock_depth` and `ts_lock_enters`, returns a token. Entering while `ts_lock_depth > 0` records a nesting error (BLOC never nests).
- `ts_lock_exit(token)` checks the token matches and decrements the depth.
- `tearDown()` fails the test if `ts_lock_depth != 0` or a nesting/token error was recorded.
- `TS_EXPECT_LOCKS(n, stmt)` asserts that `stmt` entered the lock exactly `n` times.
- `extern void (*ts_lock_exit_hook)(void);` is called (if set) inside `ts_lock_exit` before the depth is decremented; TS-03 uses it to observe state at unlock time. `tearDown` resets it to `NULL`.

### 5.4 Helpers

- `ts_fill(uint8_t *p, size_t n, uint8_t seed)` writes the pattern `seed, seed+1, …`; `ts_check_fill(...)` verifies it.
- `ts_xorshift32(uint32_t *state)` deterministic PRNG for model tests.
- `ts_pool_setup(bloc_pool_t *p, bloc_count_t n, bloc_size_t e)` = `ts_storage` + `bloc_pool_init` + `TEST_ASSERT_EQUAL(BLOC_OK, …)`.
- `ts_expected_stride(e)`, `ts_expected_header()` computed from first principles (`sizeof`, `_Alignof`, spec formula) for layout tests, independent of the library macros.

### 5.5 `setUp` / `tearDown`

Every test file uses the same pair: `setUp` resets arena, assertion and lock state; `tearDown` checks guards, unexpected assertions and lock balance.

---

## 6. Test configurations

### 6.1 Principle

Each configuration is a separate CMake build directory. The library and all tests in that directory are compiled with the same config header. Tests adapt to the configuration through the public macros and `#if` on configuration macros.

### 6.2 Configuration matrix

| Name | Coverage-gated | BA | PA | `SIZE_T` | `COUNT_T` | `REFCOUNT_T` | CHECKS | DEBUG | STATS | THREAD_SAFE | Purpose |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `default` | yes | defaults | defaults | defaults | defaults | defaults | 1 | 0 | 0 | 0 | Library defaults, no config header |
| `debug` | yes | 4 | 4 | `uint16_t` | `uint8_t` | `uint8_t` | 1 | 1 | 1 | 1 (tracer) | All debug paths, stats, lock tracing |
| `nochecks` | yes | 4 | 4 | `uint16_t` | `uint8_t` | `uint8_t` | 0 | 0 | 0 | 0 | Minimal build; always-on checks only |
| `wide` | yes | 1 | 8 | `uint32_t` | `uint16_t` | `uint16_t` | 1 | 0 | 1 | 1 (tracer) | Large types, `BA < PA` |
| `noalign` | yes | 1 | 1 | `uint16_t` | `uint8_t` | `uint8_t` | 1 | 0 | 0 | 0 | Alignment disabled |
| `bigalign` | yes | 64 | 16 | `uint16_t` | `uint8_t` | `uint32_t` | 1 | 1 | 0 | 0 | `BA > PA`, cache-line blocks, 32-bit refcount |
| `pthread` | no | 4 | 4 | `uint16_t` | `uint16_t` | `uint16_t` | 1 | 0 | 0 | 1 (pthread mutex) | Thread stress under TSan only |

All test configurations except `default` route `BLOC_PLATFORM_ASSERT(msg)` to `ts_assert_fail(msg)`. The `pthread` configuration defines the protect macros around one global `pthread_mutex_t` from `test/support/ts_pthread.c`. All configuration headers except `cfg_pthread.h` must compile freestanding for the tier-2 cross targets (section 3.6).

### 6.3 Applicability tags used in section 8

| Tag | Meaning |
| --- | --- |
| ALL | Runs in every configuration |
| CHK | Only when `BLOC_CHECKS == 1` |
| DBG | Only when `BLOC_DEBUG == 1` |
| NDBG | Only when `BLOC_DEBUG == 0` |
| TS | Only when `BLOC_THREAD_SAFE == 1` and the lock tracer is active |
| STATS | Only when `BLOC_STATS == 1` |
| PA>1 / SA>1 | Only when the given alignment is greater than one |
| DEF | Only in `default` |

Guard tests with `#if` so that they are compiled out where not applicable; do not use `TEST_IGNORE`.

### 6.4 White-box techniques allowed in tests

Fields of `struct bloc_handle` and `struct bloc_pool` are visible. Tests may:

- **read** any field to assert state;
- **prime** `refcount` near `BLOC_REFCOUNT_MAX` or `alloc_failures` near `BLOC_COUNT_MAX` to test saturation without long loops, and restore a consistent state before `tearDown`;
- **corrupt** state only in tests tagged DBG that verify a debug assertion (e.g. set `active_count` above `element_count`, clear `link.pool`, build a fake handle on the stack), and restore it afterwards.

Every other test uses the public API only.

---

## 7. Coverage procedure

### 7.1 Gate

For each coverage-gated configuration in section 6.2:

```sh
cmake --preset cov-<cfg>            # -DBLOC_TEST_CONFIG=<cfg> -DBLOC_COVERAGE=ON, GCC
cmake --build --preset cov-<cfg>
ctest --preset cov-<cfg> --output-on-failure
gcovr --root . --filter 'src/' --object-directory build/cov-<cfg> \
      --exclude-unreachable-branches \
      --fail-under-line 100 --fail-under-branch 100 --fail-under-function 100 \
      --txt --html-details build/cov-<cfg>/coverage.html
```

`scripts/coverage.sh` runs this loop for all six coverage configurations and exits non-zero on the first failure. Each configuration must reach 100 % on its own; merging reports across configurations is not allowed.

### 7.2 What counts

- Scope: `src/bloc.c` only. Headers contain no executable code (R-06).
- Lines, branches (every outcome of every `if`, `&&`, `||`, `?:`, loop condition) and functions, including `static` helpers.
- Code removed by the preprocessor in a configuration does not count for that configuration.

### 7.3 When a branch cannot be covered

Follow Q-03. Typical fixes: drop a check the spec does not demand, turn `if (a && b)` into two `if`s that tests can drive separately, move a debug-only computation inside `#if BLOC_DEBUG`. Record non-obvious restructurings in the phase commit message.

---

## 8. Test requirements (test catalogue)

Each requirement below is one Unity test function named `test_<ID>_<short_name>` (e.g. `test_POOL_04_zero_count`). A requirement may loop over a parameter table inside one test. "Unchanged" means: every field of the pool and of all handles in the test, and every byte of storage and guard bands, is identical before and after the call.

General requirements for **every** test:

- T-GEN-01: Creates its pools with `ts_storage` and checks guard bands in `tearDown`.
- T-GEN-02: Fails on any unexpected assertion and on any lock imbalance (section 5).
- T-GEN-03: Derives sizes, alignments and limits from public macros, never from literals.
- T-GEN-04: Is independent: no state carried between tests.
- T-GEN-05: Checks the return value of every BLOC call it makes.

### 8.1 Compile-time and layout (`test_layout.c`, `test/compile_fail/`)

| ID | Tag | Requirement |
| --- | --- | --- |
| CFG-01 | ALL | `BLOC_POOL_SIZE` is an integer constant expression: it sizes a file-scope array and appears in a `_Static_assert`. |
| CFG-02 | ALL | `BLOC_POOL_SIZE(n, e)` equals `n * ts_expected_stride(e)` for n ∈ {1, 2, 7} × e ∈ {1, PA−1 (if > 0), PA, PA+1, SA−1, SA, SA+1, 255}. |
| CFG-03 | ALL | `BLOC_STORAGE_ALIGNMENT == max(BA, PA, _Alignof(struct bloc_handle))` and is a power of two. |
| CFG-04 | ALL | `BLOC_HEADER_SIZE >= sizeof(struct bloc_handle)` and `BLOC_HEADER_SIZE % PA == 0`. |
| CFG-05 | ALL | `offsetof(struct bloc_handle, link) == 0` and `offsetof(…, refcount) >= sizeof(void *)` (the free-list pointer never overlays `refcount`). Field order `link, offset, len, refcount` (offsets strictly increasing). |
| CFG-06 | ALL | `BLOC_SIZE_MAX`, `BLOC_COUNT_MAX`, `BLOC_REFCOUNT_MAX` equal `(T)-1` of the configured types. |
| CFG-07 | ALL | `BLOC_ELEMENT_SIZE_MAX` is the largest `e` with `BLOC_BLOCK_STRIDE(e) <= BLOC_SIZE_MAX`: stride of `e` fits, stride of `e+1` does not. |
| CFG-08 | DEF | Every default in spec section 6 holds (`#if` checks plus `TEST_ASSERT`s on `sizeof` of the typedefs). |
| CFG-09 | ALL | `BLOC_POOL_STORAGE` yields an array of the right size whose address is aligned to `BLOC_STORAGE_ALIGNMENT`. |
| CFG-10 | ALL | Read-only API accepts `bloc_const_handle_t` (compile test: pass a const handle to every read-only function). |
| CFG-11 | ALL | `bloc_status_t` values: `BLOC_OK == 0`, then `INVALID, BOUNDS, BUSY, OVERFLOW, ALIGNMENT` in that order; `BLOC_EMPTY` is not defined. |

Compile-fail tests: each is a tiny `.c` file plus a config header; the CMake target is `EXCLUDE_FROM_ALL`, and a CTest test builds it (`cmake --build . --target <cf>`) and passes only if the build output matches the given regex (`PASS_REGULAR_EXPRESSION`), so a failure for an unrelated reason is caught. Compile-fail targets use `-std=c11 -Werror` without `-Wpedantic`. They run in every build directory, so with both host compilers, and the regex must match the diagnostics of both GCC and Clang.

| ID | Bad configuration | Expected message (regex) |
| --- | --- | --- |
| CF-01 | `BLOC_BLOCK_ALIGNMENT 3` | `BLOC_BLOCK_ALIGNMENT must be a power of two` |
| CF-02 | `BLOC_BLOCK_ALIGNMENT 0` | `BLOC_BLOCK_ALIGNMENT must be a power of two` |
| CF-03 | `BLOC_PAYLOAD_ALIGNMENT 6` | `BLOC_PAYLOAD_ALIGNMENT must be a power of two` |
| CF-04 | `BLOC_SIZE_T int16_t` | `BLOC_SIZE_T must be unsigned` |
| CF-05 | `BLOC_COUNT_T int8_t` | `BLOC_COUNT_T must be unsigned` |
| CF-06 | `BLOC_REFCOUNT_T int` | `BLOC_REFCOUNT_T must be unsigned` |
| CF-07 | `BLOC_THREAD_SAFE 1`, no protect macros | `BLOC_THREAD_SAFE requires BLOC_DECL_PROTECT` |
| CF-08 | `BLOC_DEBUG 1`, `BLOC_CHECKS 0` | `BLOC_DEBUG requires BLOC_CHECKS` |
| CF-09 | `BLOC_SIZE_T` = a typedef of `unsigned __int128` declared with `__extension__` (only registered where the compiler supports `__int128`) | `BLOC_SIZE_T must not be wider than size_t` |
| CF-10 | `BLOC_STATS 0`, code declares a `bloc_pool_stats_t` and calls `bloc_pool_get_stats` | `bloc_pool_stats_t\|bloc_pool_get_stats` |
| CF-11 | Passing a `bloc_const_handle_t` to `bloc_set_len` | `const\|qualifier` |

### 8.2 Pool lifecycle (`test_pool.c`)

| ID | Tag | Requirement |
| --- | --- | --- |
| POOL-01 | ALL | Init with valid arguments returns `BLOC_OK`; `bloc_pool_free_count == element_count`; `active_count == 0`. |
| POOL-02 | ALL | `pool == NULL` → `BLOC_INVALID`, storage unchanged. In DBG exactly one assertion. |
| POOL-03 | ALL | `storage == NULL` → `BLOC_INVALID`, pool unchanged. |
| POOL-04 | ALL | `element_count == 0` → `BLOC_INVALID`, pool and storage unchanged. |
| POOL-05 | ALL | `element_size == 0` → `BLOC_INVALID`, unchanged. |
| POOL-06 | ALL | `element_size == BLOC_ELEMENT_SIZE_MAX` passes check 2 (proved by a too-small `storage_size` yielding `BLOC_BOUNDS`); `BLOC_ELEMENT_SIZE_MAX + 1` and `BLOC_SIZE_MAX` → `BLOC_INVALID`. |
| POOL-07 | SA>1 | Storage misaligned by 1 and by `SA/2` → `BLOC_ALIGNMENT`, unchanged. |
| POOL-08 | ALL | `storage_size == BLOC_POOL_SIZE(n, e)` → OK; `BLOC_POOL_SIZE(n, e) − 1` → `BLOC_BOUNDS`; `0` → `BLOC_BOUNDS`; all failures leave pool and storage unchanged. |
| POOL-09 | ALL | `storage_size = SIZE_MAX` with a correctly sized region → OK and nothing written beyond `n * stride` (guard bands intact). |
| POOL-10 | ALL | Surplus storage: region of `BLOC_POOL_SIZE(n, e) + 3 * SA` bytes; the surplus keeps its `0x5A` fill after init, alloc of all blocks and full writes. |
| POOL-11 | ALL | Check order: (`storage == NULL` and `count == 0`) → `INVALID`; (element too large and misaligned) → `INVALID`; (misaligned and too small) → `ALIGNMENT`. |
| POOL-12 | ALL | After init, allocating all blocks yields addresses `storage + i * BLOC_BLOCK_STRIDE(e)` for i = 0, 1, … in ascending order. |
| POOL-13 | DEF | `element_count == BLOC_COUNT_MAX` with `element_size == 1` initializes and allocates all blocks. |
| POOL-14 | ALL | Failed init leaves a pattern-filled `bloc_pool_t` byte-identical (use `memcmp`) for every failing case of POOL-02..08. |
| POOL-15 | ALL | Deinit of an idle pool → OK; afterwards `storage == NULL`, `element_count == 0`, `free_count == 0`, `bloc_alloc` → `NULL` (DBG: one assertion), storage bytes unchanged by deinit. |
| POOL-16 | ALL | Deinit with an active buffer → `BLOC_BUSY`, pool unchanged, no assertion; buffer still usable; after release, deinit → OK. |
| POOL-17 | ALL | Deinit of `NULL` (CHK) → `BLOC_INVALID`; deinit twice → second call `BLOC_INVALID` (DBG: one assertion). |
| POOL-18 | ALL | Re-init after deinit with different `element_count` and `element_size` on the same storage works; new layout verified as in POOL-12. |
| POOL-19 | ALL | A zero-initialized `bloc_pool_t` behaves as uninitialized: `free_count == 0` (no assertion), alloc → `NULL` and deinit → `BLOC_INVALID` (DBG: one assertion each). |
| POOL-20 | CHK | `bloc_pool_free_count(NULL) == 0` (DBG: one assertion). |
| POOL-21 | ALL | Two pools with different geometry are independent: alloc/release in one never changes the other's fields or storage. |
| POOL-22 | TS | Lock usage: deinit (OK, BUSY and uninitialized paths) enters the lock exactly once; init enters it zero times; free_count once. |
| POOL-23 | STATS | `bloc_pool_get_stats`: `NULL` pool, `NULL` out, uninitialized pool → `BLOC_INVALID` (DBG: assertion for each); valid → `BLOC_OK`, both counters `0` right after init. |

### 8.3 Allocation (`test_alloc.c`)

| ID | Tag | Requirement |
| --- | --- | --- |
| ALLOC-01 | ALL | Alloc with headroom 0: non-NULL, `refcount == 1`, `len == 0`, `headroom == 0`, `tailroom == element_size`, `bloc_data == block + BLOC_HEADER_SIZE`, `link.pool == &pool`, free count −1. |
| ALLOC-02 | ALL | Headroom table h ∈ {0, 1, PA−1, PA, PA+1, 2·PA+1, element_size − PA, element_size}: `headroom == align_up(h, PA)` when `≤ element_size`, else `NULL`; `tailroom == element_size − headroom`; `bloc_data % PA == 0`. Choose `element_size` as a multiple of PA so `h = element_size` succeeds. |
| ALLOC-03 | ALL | Headroom whose rounding exceeds `element_size` (incl. `BLOC_SIZE_MAX`) → `NULL`; pool unchanged; no assertion; STATS: `alloc_failures` unchanged; TS: lock not entered. |
| ALLOC-04 | ALL | Exhaustion: `n` allocs succeed, the next returns `NULL` with no assertion; `free_count == 0`. |
| ALLOC-05 | CHK | `bloc_alloc(NULL, 0)` → `NULL` (DBG: one assertion). |
| ALLOC-06 | ALL | Alloc on zero-initialized pool and on deinitialized pool → `NULL` (DBG: one assertion each). |
| ALLOC-07 | ALL | All handles from one pool are distinct; data areas of different blocks do not overlap. |
| ALLOC-08 | ALL | Writing `element_size` bytes into every block (headroom 0, `bloc_set_len(element_size)`, `memset`) leaves every other block's `offset`, `len`, `refcount`, `link` unchanged and guard bands intact. |
| ALLOC-09 | ALL | LIFO reuse: alloc A, B; release A; next alloc returns A. Release B then A; next two allocs return A then B. |
| ALLOC-10 | ALL | `bloc_calloc`: after dirtying a block with `0xFF` and releasing it, calloc returns it with all `element_size` bytes from `data_start` zero, `len == 0`, headroom rounded as in ALLOC-02. |
| ALLOC-11 | ALL | `bloc_calloc` failure paths (empty pool, oversize headroom; CHK: NULL pool) return `NULL` and write nothing (storage unchanged). |
| ALLOC-12 | STATS | `high_water` tracks the maximum: alloc 3, release 2, alloc 1 → `high_water == 3`; then alloc to full → `== n`. |
| ALLOC-13 | STATS | `alloc_failures` increments once per empty-pool failure and saturates at `BLOC_COUNT_MAX` (prime to `BLOC_COUNT_MAX − 1`, fail twice, expect `BLOC_COUNT_MAX`). |
| ALLOC-14 | STATS | Re-init resets both counters to 0. |
| ALLOC-15 | ALL | Every block's `data_start` is aligned to PA and every block start to `BLOC_STORAGE_ALIGNMENT` (loop over all blocks). |
| ALLOC-16 | TS | Successful alloc and empty-pool alloc enter the lock exactly once; calloc enters exactly once; parameter failures (CHK) zero times. |
| ALLOC-17 | DBG | Corrupt `active_count = element_count` with one free block left, alloc → assertion fires once (invariant), handle still returned (continue semantics); restore. |

### 8.4 Reference counting (`test_refcount.c`)

| ID | Tag | Requirement |
| --- | --- | --- |
| REF-01 | ALL | Retain increments `refcount`; one release afterwards leaves the block allocated (free count unchanged); a second release frees it. |
| REF-02 | CHK | `bloc_retain(NULL)` → `BLOC_INVALID` (DBG: one assertion). |
| REF-03 | ALL | Retain on a released (free) block → `BLOC_INVALID`, unchanged (DBG: one assertion, lock depth 0 at assertion). |
| REF-04 | ALL | Prime `refcount = BLOC_REFCOUNT_MAX − 1`; retain → OK (`== MAX`); retain → `BLOC_OVERFLOW`, `refcount` still `MAX`, no assertion; restore `refcount = 1`, release frees. |
| REF-05 | ALL | `bloc_release(NULL)` → `BLOC_OK`, nothing changes; TS: lock not entered. |
| REF-06 | ALL | Release with `refcount > 1` decrements only; block stays allocated; `active_count` unchanged. |
| REF-07 | ALL | Final release: `refcount == 0`, `active_count − 1`, block becomes `free_head`, the next alloc returns it. |
| REF-08 | ALL | Double release → second call `BLOC_INVALID`; `active_count` not decremented again; free list intact: allocating `n` blocks afterwards yields `n` distinct handles and the next alloc `NULL` (DBG: one assertion). |
| REF-09 | ALL | Release of a block that was never allocated (computed address of a free block) → `BLOC_INVALID`, unchanged. |
| REF-10 | ALL | Release returns the block to its own pool when several pools exist (no pool argument needed). |
| REF-11 | TS | Retain and release enter the lock exactly once on every path except `release(NULL)` and CHK `retain(NULL)`; depth 0 afterwards. |

### 8.5 Accessors (`test_access.c`)

| ID | Tag | Requirement |
| --- | --- | --- |
| ACC-01 | ALL | `bloc_data`, `bloc_len`, `bloc_headroom`, `bloc_tailroom` return the values defined in spec section 10 after alloc and after each of: `set_len`, `add_header`, `remove_header`, `append_data`, `prepend_data`, `copy_from`. `headroom + len + tailroom == element_size` always. |
| ACC-02 | CHK | `NULL` handle → `bloc_data == NULL`, others `0` (DBG: one assertion each). |
| ACC-03 | ALL | Accessors work at `refcount > 1` without assertion. |
| ACC-04 | TS | Accessors never enter the lock. |

### 8.6 Length operations (`test_length.c`)

| ID | Tag | Requirement |
| --- | --- | --- |
| LEN-01 | ALL | `set_len` to `element_size − offset` → OK; to that `+ 1` → `BLOC_BOUNDS`, unchanged. Also with `len = BLOC_SIZE_MAX` → `BLOC_BOUNDS`. |
| LEN-02 | ALL | `set_len` shrinks (to smaller value and to 0); `offset` unchanged. |
| LEN-03 | ALL | `set_len` never modifies payload bytes (pattern check over the whole data area). |
| LEN-04 | ALL | `add_header(0)` → OK, unchanged; `add_header(offset)` → OK, `headroom == 0`, `bloc_data` moved back by `offset`, existing payload bytes unchanged at their addresses; `add_header(offset + 1)` and `add_header(BLOC_SIZE_MAX)` → `BLOC_BOUNDS`, unchanged. |
| LEN-05 | ALL | `remove_header(0)` → unchanged; `remove_header(len)` → `len == 0`, `offset` advanced by old len; `remove_header(len + 1)` → `BLOC_BOUNDS`. |
| LEN-06 | ALL | `remove_header(k)` followed by `add_header(k)` restores the original view exactly. |
| LEN-07 | CHK | Each of the three functions with `NULL` → `BLOC_INVALID` (DBG: one assertion each). |
| LEN-08 | DBG | Each of the three with `refcount == 2` → one assertion and the operation is still performed. |
| LEN-09 | TS | None of the three enters the lock. |

### 8.7 Copy (`test_copy.c`)

| ID | Tag | Requirement |
| --- | --- | --- |
| CPY-01 | ALL | `copy_from` writes `n` bytes at `bloc_data`, sets `len = n`, keeps `offset`, leaves bytes after `n` and headroom bytes unchanged. |
| CPY-02 | ALL | `copy_from` with `n == element_size − offset` → OK; `+1` → `BLOC_BOUNDS`, buffer unchanged (len and bytes). |
| CPY-03 | ALL | `copy_from` with `n == 0` → OK, `len == 0`, no byte written. |
| CPY-04 | CHK | `copy_from` with NULL handle or NULL source (also with `n == 0`) → `BLOC_INVALID`. |
| CPY-05 | ALL | `copy_to` table: `(pos, n)` ∈ {(0, len), (3, len−3), (len, 0), (0, 0)} → OK, correct bytes, destination bytes after `n` untouched; {(len+1, 0), (2, len−1), (BLOC_SIZE_MAX, 1), (1, BLOC_SIZE_MAX)} → `BLOC_BOUNDS`, destination untouched. Buffer never changes. |
| CPY-06 | CHK | `copy_to` with NULL handle or NULL destination → `BLOC_INVALID`. |
| CPY-07 | ALL | `bloc_copy` between buffers with different offsets (src headroom `PA`, dst headroom `2·PA`): dst payload equals src payload, dst offset preserved, `dst.len == src.len`, src unchanged. |
| CPY-08 | ALL | `bloc_copy` across pools of different element sizes: fits → OK; `src.len > dst_element_size − dst.offset` → `BLOC_BOUNDS`, dst unchanged. |
| CPY-09 | ALL | `bloc_copy(b, b)` → OK, unchanged, no assertion even when `refcount == 2`. |
| CPY-10 | ALL | `bloc_copy` from an empty source → `dst.len == 0`. |
| CPY-11 | CHK | `bloc_copy` with NULL dst or NULL src → `BLOC_INVALID`. |
| CPY-12 | ALL | `copy_to` works at `refcount > 1` without assertion. |

### 8.8 Append (`test_append.c`)

| ID | Tag | Requirement |
| --- | --- | --- |
| APP-01 | ALL | `append_data` to an empty and to a non-empty buffer: bytes appear after the old payload in order, `len += n`, `offset` unchanged, bytes beyond the new end unchanged. |
| APP-02 | ALL | `append_data` with `n == tailroom` → OK (tailroom becomes 0); `n == tailroom + 1` → `BLOC_BOUNDS`, unchanged; `n == BLOC_SIZE_MAX` → `BLOC_BOUNDS`. |
| APP-03 | ALL | `bloc_append` copies the **first** `n` bytes of the source payload (source with non-zero headroom); `n > src.len` → `BLOC_BOUNDS`; `n > tailroom(dst)` → `BLOC_BOUNDS`; dst unchanged on failure; src always unchanged. |
| APP-04 | ALL | `bloc_append(b, b, n)` with `n ≤ len` duplicates the first `n` bytes at the end; `n > len` → `BLOC_BOUNDS`. |
| APP-05 | ALL | `bloc_append` across two pools with different element sizes. |
| APP-06 | ALL | `n == 0` for both variants → OK, unchanged. |
| APP-07 | CHK | NULL dst, NULL src handle, NULL data pointer → `BLOC_INVALID`. |

### 8.9 Prepend (`test_prepend.c`)

| ID | Tag | Requirement |
| --- | --- | --- |
| PRE-01 | ALL | `prepend_data` into headroom: new bytes precede the old payload, `offset −= n`, `len += n`, old payload bytes unchanged at their addresses, remaining headroom bytes unchanged. |
| PRE-02 | ALL | `n == headroom` → OK (headroom 0); `n == headroom + 1` and `BLOC_SIZE_MAX` → `BLOC_BOUNDS`, unchanged. |
| PRE-03 | ALL | Alloc with headroom 0, then `prepend_data(…, 1)` → `BLOC_BOUNDS`. |
| PRE-04 | ALL | `bloc_prepend` copies the first `n` bytes of the source payload; `n > src.len` → `BLOC_BOUNDS`; `n > headroom(dst)` → `BLOC_BOUNDS`; src unchanged. |
| PRE-05 | ALL | `bloc_prepend(b, b, n)` with `n ≤ len` and `n ≤ headroom` duplicates the first `n` bytes in front. |
| PRE-06 | ALL | `bloc_prepend` across pools. |
| PRE-07 | ALL | `n == 0` for both variants → OK, unchanged. |
| PRE-08 | CHK | NULL dst, NULL src handle, NULL data pointer → `BLOC_INVALID`. |
| PRE-09 | ALL | Protocol scenario from spec section 10 (TX example): headroom 40, append payload, add two 20-byte headers in place; final byte image equals the expected concatenation and `bloc_data` is 40 bytes before the original payload start. |

### 8.10 Debug checks (`test_debug.c`, tag DBG)

| ID | Requirement |
| --- | --- |
| DBG-01 | Table-driven over every public function that takes a handle (except retain/release), and for two-handle functions separately for `dst` and `src`: a fake handle on the stack (`refcount = 1`, `link.pool = &pool`, outside storage) → exactly one assertion and the bail return value (`BLOC_INVALID` / `NULL` / `0`), no side effect. |
| DBG-02 | Same table with a fake handle inside storage but not on a stride boundary: use a pool with `element_size ≥ 2·SA + sizeof(struct bloc_handle)`, allocate block 0 with headroom 0 and write the fake header at `block0 + BLOC_ALIGN_UP(BLOC_HEADER_SIZE, BLOC_STORAGE_ALIGNMENT)`, which lies in block 0's data area and is correctly aligned for a handle. |
| DBG-03 | Same table with a real handle whose `link.pool` is temporarily set to `NULL`. |
| DBG-04 | Same table with a real handle whose pool is temporarily marked uninitialized (`storage = NULL`). |
| DBG-05 | Same table with a handle at `storage + element_count * stride` (one past the end, using a region with surplus space). |
| DBG-06 | Same table with a free block (`refcount == 0`). |
| DBG-07 | Retain and release with each defect of DBG-01..05 → one assertion, `BLOC_INVALID`, lock depth 0 at the assertion, state unchanged. |
| DBG-08 | Overlap: `copy_from`, `append_data`, `prepend_data` with the external pointer inside the destination write range → one assertion, `BLOC_INVALID`, no byte written. Adjacent but non-overlapping ranges (source ends exactly where the write starts, and starts exactly where it ends) → no assertion, OK. `n == 0` with a pointer inside the range → no assertion. |
| DBG-09 | Shared mutation: every mutating function (spec section 9 list) at `refcount == 2` → one assertion, operation performed, return `BLOC_OK`. The same calls at `refcount == 1` → no assertion. |
| DBG-10 | Release invariant: corrupt `active_count` to `element_count + 1` before a final release → one assertion after unlock, release still completes; restore. |
| DBG-11 | Every CHK failure listed in 8.2–8.9 produces exactly one assertion in this configuration (covered by those tests' DBG notes; this test re-checks a representative per function). |
| DBG-12 | Assertions never fire with the lock held: across the whole DBG suite `ts_assert_lock_depth` is always 0. |
| DBG-13 | Valid runtime conditions never assert: empty pool, oversize headroom, refcount overflow, `BLOC_BUSY` deinit. |

### 8.11 Thread safety (`test_thread.c`, `test_stress_pthread.c`)

| ID | Tag | Requirement |
| --- | --- | --- |
| TS-01 | TS | Lock usage table for every public function and path, matching spec section 12: protected functions enter exactly once per call on every path after their parameter checks (the `NULL` checks and the headroom check in `bloc_alloc` count as parameter checks and run before the lock); unprotected functions enter zero times. |
| TS-02 | TS | No nesting: `ts_lock_depth` never exceeds 1. |
| TS-03 | TS | `bloc_calloc` zeroes outside the lock: a `ts_lock_exit_hook` records that the block's data area is still dirty when the lock is released; after the call it is zero. |
| TS-04 | `pthread` | Stress: 8 threads × 100 000 iterations of {alloc, optional retain + release, write pattern, verify pattern, release} on one pool of 16 blocks; afterwards `free_count == element_count`, all blocks allocatable, no pattern mismatch. Runs under ThreadSanitizer with zero reports. Not coverage-gated. |

### 8.12 Model-based test (`test_model.c`, tag ALL)

| ID | Requirement |
| --- | --- |
| MODEL-01 | A reference model (plain arrays: per buffer `offset`, `len`, bytes, `refcount`, allocated flag) is driven in lockstep with BLOC by a seeded `ts_xorshift32` sequence of 20 000 random operations over two pools (all public operations, with random lengths that are valid about 70 % of the time). After every step: return codes equal, every live buffer's `offset`, `len`, `refcount` and payload bytes equal, `free_count` equal, guard bands intact. Run with three fixed seeds. On mismatch, print seed, step and operation. |
| MODEL-02 | After the random run, release every live buffer; the pool returns to `free_count == element_count` and deinit returns `BLOC_OK`. |

---

## 9. Verification outside unit tests

### 9.1 No-heap and dependency check (`scripts/check_no_heap.sh`)

| ID | Requirement |
| --- | --- |
| NH-01 | Host check (the cross-target equivalent is XC-02). Build the library in release mode (`-O2`, no coverage, no sanitizers, `-fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0`; Ubuntu's GCC enables fortification by default, which would turn `memcpy` into `__memcpy_chk`) for configurations `default` and `debug`. Run `nm -u` on the object file. The set of undefined symbols must be a subset of `{memcpy, memset}` (plus the test hooks `ts_assert_fail`, `ts_lock_enter`, `ts_lock_exit` in `debug`). Any other symbol fails the check, and the script prints it. |
| NH-02 | Same check for a `debug` build that uses the **default** `BLOC_PLATFORM_ASSERT` (no test hook): undefined symbols ⊆ `{memcpy, memset}`. This proves the trapping default needs no C library. |
| NH-03 | `grep` over the C sources and headers in `src/` finds no `#include` other than `bloc.h`, `bloc_opt.h`, `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`, `<string.h>`, and no occurrence of `malloc`, `calloc`, `realloc`, `aligned_alloc`, `free(`, `memmove`, `alloca` (not even in comments, so the check stays a plain grep; `bloc_calloc` is matched as a whole word and allowed). |

### 9.2 Sanitizers (`scripts/sanitize.sh`)

| ID | Requirement |
| --- | --- |
| SAN-01 | Every coverage-gated configuration built with Clang and `-fsanitize=address,undefined -fno-sanitize-recover=all`; all tests pass with zero reports. |
| SAN-02 | The `pthread` configuration built with `-fsanitize=thread`; TS-04 passes with zero reports. |
| SAN-03 | Same as SAN-01 with GCC (`-fsanitize=address,undefined -fno-sanitize-recover=all`). |

### 9.3 Host compilers (tier 1)

| ID | Requirement |
| --- | --- |
| CC-01 | All seven configurations build without warnings and pass all tests, including compile-fail tests, with GCC and with Clang at Debug (`-O0`). |
| CC-02 | 32-bit host (`-m32`, needs `gcc-multilib`): all seven configurations pass at Debug and `default` at Release; `sizeof(struct bloc_handle) == 12` in `default`. Required in CI (`host` job); optional locally, where a missing `gcc-multilib` gives a `SKIP` line. |
| CC-03 | Configurations `default`, `debug` and `nochecks` additionally pass with both compilers at Release (`-O2`) and MinSizeRel (`-Os`). This catches optimization-dependent undefined behaviour in the configuration used for footprint measurement. |
| CC-04 | Older compilers: CC-01 and CC-03 also pass with the default GCC and Clang of `ubuntu:24.04` (GCC 13, Clang 18) and `ubuntu:22.04` (GCC 11, Clang 14, the oldest supported versions of section 3.1), with zero warnings. `build_one.sh` prints `$CC --version` first, so the log shows the exact version tested. If an old compiler emits a false-positive warning that cannot be avoided by better code, disable that one warning for that compiler version only in CMake, with a comment, and record it in `docs/OPEN_QUESTIONS.md`. |

### 9.4 Cross compilers (tier 2, `scripts/cross_check.sh`)

The script loops over the tier-2 rows of section 3.6 × the applicable configurations by calling `scripts/ci/build_one.sh` for each combination (section 10.4); `--family <name>` restricts it to one CI target family. For bare-metal targets `build_one.sh` calls the compilers directly on the single translation unit (no CMake toolchain files) and prints one result line per combination. It exits non-zero if any combination fails, and skips a compiler with a visible `SKIP` line only if it is not installed. In CI and in the devcontainer every compiler is required.

| ID | Requirement |
| --- | --- |
| XC-01 | `src/bloc.c` compiles with zero warnings (`-Werror`, flags of section 3.3) for every tier-2 target and every applicable configuration. |
| XC-02 | `nm -u` on each object lists only `memcpy`, `memset` and the test hooks of the configuration (`ts_assert_fail`, `ts_lock_enter`, `ts_lock_exit`). With `BLOC_DEBUG = 1`, the target's unsigned division and modulo helpers are also allowed (`__aeabi_uidiv`, `__aeabi_uidivmod`, `__udivsi3`, `__umodsi3`, `__udivmodhi4`, `__udivmodsi4`). Any other symbol fails and is printed. A debug build with the default `BLOC_PLATFORM_ASSERT` is checked too (NH-02 equivalent). |
| XC-03 | Same as XC-01 and XC-02 with Clang for its tier-2 targets. |
| XC-04 | AVR: the `wide` configuration (`BLOC_SIZE_T uint32_t` with a 16-bit `size_t`) fails to compile with `BLOC_SIZE_T must not be wider than size_t`. This is a real-target counterpart to CF-09. |
| XC-05 | `test/target/layout_static.c` compiles for every tier-2 target and configuration. It contains the compile-time parts of CFG-01..07, CFG-09 and CFG-11 as `_Static_assert`s, with the expected values computed from `sizeof`, `_Alignof` and the spec formulas. This proves the layout macros on 8-, 32- and 64-bit targets without executing anything. |

### 9.5 Code footprint (`scripts/check_size.sh`)

Measurement follows spec section 17. The script compiles `src/bloc.c` for each target, compiler and configuration in the table below. It records `.text`, `.rodata`, `.data` and `.bss` of the object (summing all `*.text*` and `*.rodata*` input sections), plus per-function sizes from `nm --size-sort`.

Measured matrix:
- Targets: all tier-2 targets of section 3.6, plus host x86-64 for information.
- Configurations: `default` (no config header) and `nochecks`, which carry the spec budgets on ARM; also `debug` with the default assert, `wide` (except AVR), and a thread-safe configuration `cfg_size_ts.h` that defines PRIMASK-based protect macros with inline assembly, as in spec section 12 (ARM M-profile only).

| ID | Requirement |
| --- | --- |
| FP-01 | Report: a Markdown table `build/size/report.md` with one row per target × compiler × configuration (`.text`, `.rodata`, `.data`, `.bss`) and the ten largest functions per ARM row. It is printed to the console and uploaded as a CI artifact. |
| FP-02 | Spec budgets: with `arm-none-eabi-gcc`, `.text` of `default` and `nochecks` on ARMv6-M (`cortex-m0plus`) and ARMv7-M (`cortex-m3`) does not exceed the budgets of spec section 17. Exceeding them fails. |
| FP-03 | Section rules: `.data == 0` and `.bss == 0` for every row; `.rodata == 0` for every row with `BLOC_DEBUG = 0`. |
| FP-04 | Regression baseline: `scripts/size_baseline.txt` stores one line per measured row (`target compiler config compiler-version text-bytes`). The check fails if `.text` is larger than the baseline. If it is smaller, the script prints a reminder to lower the baseline; the lower value must be committed in the same phase. If the compiler version differs from the recorded one, the row is reported but not gated, so developers with other toolchains get no false failures. CI uses the devcontainer versions, so it is always gated. |
| FP-05 | Dead-stripping: `test/size/min_app.c` calls only `bloc_pool_init`, `bloc_alloc` and `bloc_release`. Linked for ARMv6-M with `-ffunction-sections -Wl,--gc-sections --specs=nosys.specs`, the final ELF contains no other `bloc_` function (`nm` check), and its BLOC contribution is smaller than the FP-02 measurement of the full API. |
| FP-06 | Size and correctness are measured on identical code. The footprint objects use the same sources and configurations as the tier-1 tests; CC-03 runs those configurations at `-Os` on the host. |

`check_size.sh` also accepts `--family <name>`; in CI each bare-metal family job measures its own rows, appends its part of the FP-01 table to the job summary (`$GITHUB_STEP_SUMMARY`) and uploads it as artifact `size-<family>`. Run without arguments (locally), it produces the complete `build/size/report.md`.

### 9.6 Emulated targets (tier 1E)

| ID | Requirement |
| --- | --- |
| EM-01 | For each tier-1E target of section 3.6, all seven configurations build with the target's toolchain file at Debug with zero warnings, and the complete CTest suite passes under `qemu-<arch>`. |
| EM-02 | `default` and `nochecks` additionally pass at MinSizeRel (`-Os`) on every tier-1E target. |
| EM-03 | Symbol check on the MinSizeRel library object of `default`, and of `debug` with the default `BLOC_PLATFORM_ASSERT`, built with `-fno-stack-protector -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0`: undefined symbols ⊆ `{memcpy, memset}` (debug may add the division helpers allowed by R-03). Uses `<triple>-nm`. |
| EM-04 | `test/test_platform.c` is built only when the build passes `BLOC_EXPECT_BIG_ENDIAN` and `BLOC_EXPECT_PTR_BITS` (set by `build_one.sh` from the target table). It asserts at run time that the byte order (checked through a `uint32_t`/`uint8_t[4]` union) and `sizeof(void *) * CHAR_BIT` match the table. This proves that each emulated job really executed code for the architecture it claims, and not, for example, a host binary. It is in `test/`, not `src/`, so coverage is unaffected (R-08). |

Layout tests need no per-target expectations: by Q-06 they derive every expected value from `sizeof`, `_Alignof` and the spec formulas, so they are automatically valid on 32-bit and big-endian targets.

### 9.7 Link-time optimization

| ID | Requirement |
| --- | --- |
| LTO-01 | With `BLOC_LTO=ON` (`-O3 -fstrict-aliasing`, interprocedural optimization across library, test support and tests), configurations `default`, `debug` and `nochecks` pass all tests with GCC and with Clang. |
| LTO-02 | The LTO links are warning-free with `-Werror` at link time. GCC's `-Wlto-type-mismatch` (on by default) then reports any translation unit that sees a different `struct bloc_handle` or `struct bloc_pool` than the library, which is a second, independent guard for CM-06. |

Rationale: storage is a `uint8_t` array that the library accesses through `struct bloc_handle` lvalues (spec section 4, the lwIP `memp` idiom). LTO is the only build in which the optimizer sees the test's storage declaration and the library's accesses at the same time, so it is the only place where a type-based aliasing assumption can surface. No sanitizer substitutes: UBSan does not model strict aliasing. If LTO-01 fails because of aliasing, this is a spec-level question; record it in `docs/OPEN_QUESTIONS.md`, and do not hide it with `-fno-strict-aliasing`.

### 9.8 FetchContent consumer smoke tests (`scripts/fetchcontent_smoke.sh`)

`test/fetchcontent_smoke/` is a stand-alone consumer project. It is never added by BLOC's own `CMakeLists.txt`; it is configured from outside, like a real user project. Its `CMakeLists.txt` contains the `bloc-fetchcontent` block of section 3.8 (with `BLOC_GIT_TAG` defaulting to `main` and `BLOC_GIT_SHALLOW` to `TRUE`), an executable `smoke` from `main.c` linked to `bloc::bloc`, `enable_testing()` and `add_test(NAME smoke COMMAND smoke)`, plus the configure-time hygiene assertions of FC-02. The consumer compiles `main.c` with its own strict flags: `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`.

`main.c` uses only the public API and returns a distinct non-zero code for each failed check (no `stdio`, so it also links bare-metal):

1. Declares storage with `BLOC_POOL_STORAGE` for N = 4 elements of E = 64 bytes; `bloc_pool_init` returns `BLOC_OK`.
2. **Layout cross-check (the ODR detector).** Allocates two buffers with headroom 8. Checks that the first handle is at the storage base, that the distance between the two handles equals `BLOC_BLOCK_STRIDE(E)`, and that `bloc_data(h) - (uint8_t *)h == BLOC_HEADER_SIZE + 8`. These values are computed by the library on one side and by the consumer's macros on the other, so they only agree if both were compiled with the same configuration. This check runs before anything else that depends on the layout.
3. From phase 4: appends a payload with `bloc_append_data`, adds a 4-byte header with `bloc_add_header`, reads both back with `bloc_copy_to` and compares them.
4. With `BLOC_STATS`: `bloc_pool_get_stats` reports a high-water mark of 2.
5. Releases both buffers; `bloc_pool_free_count` is N; `bloc_pool_deinit` returns `BLOC_OK`.

`config/smoke_opts.h` is the consumer's configuration header: `BLOC_BLOCK_ALIGNMENT 32`, `BLOC_PAYLOAD_ALIGNMENT 16`, `BLOC_SIZE_T uint32_t`, `BLOC_COUNT_T uint16_t`, `BLOC_STATS 1`. Every type is at least as wide as the default, so in the deliberate mismatch of FC-04 the library (default layout) never writes past the consumer's (larger) structures, and the test fails cleanly instead of corrupting memory.

`scripts/fetchcontent_smoke.sh <variant>...` runs the variants below. Each one uses a fresh build directory `build/fc/<variant>` and its own `FETCHCONTENT_BASE_DIR`, and, except for `online`, passes `-DFETCHCONTENT_SOURCE_DIR_BLOC=<repo root>`, so the current checkout is consumed without network access. Without arguments it runs all offline variants.

| ID | Variant | Requirement |
| --- | --- | --- |
| FC-01 | `basic` | Fresh configure, build and `ctest` of the smoke project with no BLOC options set: `smoke` passes. Proves that consumption works and that the public headers are warning-free under the consumer's strict flags. |
| FC-02 | (all variants) | Hygiene, asserted at configure time in the smoke `CMakeLists.txt` with `FATAL_ERROR`: the `BUILDSYSTEM_TARGETS` directory property of `${bloc_SOURCE_DIR}/src` is exactly `bloc`, and its `SUBDIRECTORIES` property is empty; `TARGET unity` is false, and `FetchContent_GetProperties(unity)` reports it as not populated; `INTERFACE_LINK_LIBRARIES` and `INTERFACE_COMPILE_OPTIONS` of `bloc` are empty; `COMPILE_OPTIONS` of `bloc` contain none of `-Werror`, `--coverage`, `-fsanitize`, `-flto`; cache entries that are new after `FetchContent_MakeAvailable` all match `^(BLOC_|FETCHCONTENT_)`, except `GIT_EXECUTABLE`, which FetchContent itself creates when it downloads with git (FC-09), and `BLOC_TEST_CONFIG`, `BLOC_COVERAGE`, `BLOC_SANITIZE`, `BLOC_LTO` are not defined; `CMAKE_C_FLAGS` and the consumer root directory's `COMPILE_OPTIONS`, `COMPILE_DEFINITIONS` and `INCLUDE_DIRECTORIES` are unchanged. After the build, the script checks that `ctest -N` lists exactly the smoke tests. (CM-01..05) |
| FC-03 | `config-normal`, `config-cache` | The consumer sets `BLOC_CONFIG_HEADER=smoke_opts.h` and `BLOC_CONFIG_DIRS=${CMAKE_CURRENT_SOURCE_DIR}/config`, once as normal variables and once as cache variables, before `FetchContent_MakeAvailable`. Each variant is configured twice (fresh, then reconfigure) and must produce the same result: `smoke` passes, which by check 2 proves the library and consumer share the configuration, and `INTERFACE_COMPILE_DEFINITIONS` of `bloc` is exactly `BLOC_CONFIG_HEADER="smoke_opts.h"`. A third run with a relative `BLOC_CONFIG_DIRS` must fail to configure with the documented message. (CM-06) |
| FC-04 | `mismatch` | Sensitivity of the ODR detector. A second executable `smoke_mismatch` is built from the same `main.c` against the default-configured library, but with `BLOC_CONFIG_HEADER` and the config include directory added only to the executable. It is registered with `WILL_FAIL TRUE`, so `ctest` passes only if check 2 detects the mismatch. |
| FC-05 | `cstd` | The consumer sets `CMAKE_C_STANDARD` to 11, 17 and 23 (three configures); `main.c` builds warning-free with each, and `compile_commands.json` shows the `bloc` sources still compiled with `-std=c11`. (CM-07) |
| FC-06 | `generators` | FC-01 with the `Ninja`, `Ninja Multi-Config` (building and testing both `Debug` and `Release`) and `Unix Makefiles` generators. (CM-08) |
| FC-07 | `cmake-floor` | FC-01 and FC-03 (`config-normal`) with CMake 3.20 (installed with `pip install "cmake==3.20.*"` into a venv) in addition to the current CMake. Proves that `cmake_minimum_required(VERSION 3.20...4.2)` is honest and that the CMP0126 guard of section 3.8 works. |
| FC-08 | `baremetal` | The smoke project is configured with `toolchain-cortex-m0plus.cmake` (`arm-none-eabi-gcc`, `CMAKE_SYSTEM_NAME Generic`, `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY`, `-mcpu=cortex-m0plus -mthumb`) and `smoke` is linked with `--specs=nosys.specs -Wl,--gc-sections`. It is not run; `arm-none-eabi-nm` shows `bloc_pool_init` in the ELF. (CM-08) |
| FC-09 | `online` | Without `FETCHCONTENT_SOURCE_DIR_BLOC`: the smoke project fetches `https://github.com/Andste82/bloc.git` at `BLOC_GIT_TAG=<commit sha>` (`BLOC_GIT_SHALLOW=FALSE`, because a shallow fetch cannot target a bare commit hash), then runs FC-01. CI runs it only on pushes to `main`, on tags and on the weekly schedule, because on pull requests from forks the commit is not in the repository yet. |
| FC-10 | `docs` | The `bloc-fetchcontent` block in `README.md` (phase 7) and the one in `test/fetchcontent_smoke/CMakeLists.txt` are identical after replacing the two variables with the README's literal values. The script extracts both blocks between the `# >>> bloc-fetchcontent` and `# <<< bloc-fetchcontent` markers and diffs them, so the documented way to consume BLOC is the tested one. |

---

## 10. Continuous integration (`.github/workflows/ci.yml`, `.github/actions/target`)

Goal: many compilers, many architectures, many tests, on every pull request, without the pipeline becoming slow, flaky or a wall of copy-pasted YAML. The files in `.github/` are the reference implementation; this section fixes their structure and rules.

### 10.1 Principles

1. **Gates before the matrix.** The pipeline has three stages: `lint` (format, shellcheck, NH-03; no compiler run), then the gates `coverage`, `sanitize` and `no-heap` on the reference image, then the matrix (`host`, `qemu`, `bare`, `lto`, `fetch`), whose jobs have `needs:` on every gate. A broken build, a coverage gap, a sanitizer report or a forbidden symbol is reported once by a gate instead of by every matrix cell, and no runner time is spent on a commit that is already known to be bad.
2. **Nothing is built twice.** The gates build and run the full test suite of the six coverage-gated configurations with GCC (`coverage`, `-O0`) and with GCC and Clang (`sanitize`), so they are the reference-compiler Debug runs of CC-01. The reference cells `host / gcc-15` and `host / clang-21` only add what the gates do not build: `pthread` at Debug and the optimized builds of CC-03. The online FetchContent test FC-09 is a step of `fetch`, not a separate job.
3. **One composite action, one call site per tier.** `.github/actions/target/action.yml` installs a toolchain and builds and tests one target in several configurations, parameterized by inputs (apt packages, target selector, compiler, configurations). `host`, `qemu` and `bare` call it from their `matrix:`; adding a compiler or architecture is one matrix entry. A composite action is used instead of a reusable workflow because a called workflow adds a second level to every job name.
4. **Short job names.** Display names are `<group> / <cell>`, for example `sanitize / clang`, `host / gcc-11`, `host / gcc-15-m32`, `qemu / s390x`, `bare / avr-gcc`, `lto / gcc`. The group is the job id; the cell is one short matrix key (`id`, `arch`, `family`, `cc`). Referencing the matrix in `name:` keeps GitHub from appending all other matrix values.
5. **Configurations are steps, not matrix cells.** The seven test configurations are seven steps in one job, so one toolchain installation serves all of them. Each step has `if: ${{ !cancelled() && steps.install.outcome == 'success' && <config selected> }}`, so one failing configuration does not hide the others, and each reports its own name, duration and log.
6. **Every cell answers for itself.** `strategy.fail-fast: false` everywhere: the matrices cover genuinely different compilers and hardware.
7. **Pinned environments.** Jobs run in `container: ubuntu:<version>`, so compiler versions are fixed by the image, not by GitHub's `ubuntu-latest`. The gates, the reference cells and every footprint job use `ubuntu:26.04` (= devcontainer, FP-04 baseline).
8. **Robust installs.** `scripts/ci/apt_install.sh` always adds `ca-certificates git`, sets `DEBIAN_FRONTEND=noninteractive`, uses `--no-install-recommends` and retries `apt-get update && apt-get install` three times with 15 s between attempts, printing `::warning::` per failed attempt and `::error::` at the end.
9. **Bounded jobs.** Every job has `timeout-minutes` of at most 30 (observed durations are about one minute per job). A hung job must fail fast instead of holding a runner for GitHub's default of six hours.
10. **Cancel superseded runs.** Top-level `concurrency: { group: ${{ github.workflow }}-${{ github.ref }}, cancel-in-progress: ${{ github.ref != 'refs/heads/main' }} }`. Runs on `main` are never cancelled.
11. **Least privilege.** Top-level `permissions: contents: read`.
12. **One required check.** The final job `ci-ok` has `needs:` on every job and `if: always()`, and fails if any of them failed, was cancelled or was skipped (a skipped matrix job means that a gate failed). Branch protection requires only `ci-ok`.
13. **Same scripts locally and in CI.** Workflows contain no build logic beyond calling `scripts/`. `scripts/run_all.sh` runs every tier whose tools are installed and prints `SKIP` for the rest.

Triggers: `push` to `main` and tags `v*`, `pull_request` to `main`, `workflow_dispatch`, and a weekly `schedule` (catches drift in the container images and runs FC-09).

### 10.2 Jobs

| Stage | Job (display name) | Image | Cells | Steps | Tests |
| --- | --- | --- | --- | --- | --- |
| 1 | `lint` | `ubuntu:26.04` | – | clang-format check over `src/ test/ examples/`; shellcheck over `scripts/`; `check_no_heap.sh --grep-only` | NH-03, format |
| 2 | `coverage` | `ubuntu:26.04` | – | one step per coverage-gated configuration: `scripts/coverage.sh <cfg>`; upload HTML reports | Q-01, CC-01 (GCC) |
| 2 | `sanitize / <cc>` | `ubuntu:26.04` | `gcc`, `clang` | one step per coverage-gated configuration with ASan+UBSan; `clang` adds the TSan `pthread` step | SAN-01..03, CC-01 |
| 2 | `no-heap` | `ubuntu:26.04` | – | `scripts/check_no_heap.sh` | NH-01..02 |
| 3 | `host / <id>` | per cell | `gcc-15`, `clang-21` (`ubuntu:26.04`: `pthread` at Debug, optimized step); `gcc-13`, `clang-18` (`ubuntu:24.04`), `gcc-11`, `clang-14` (`ubuntu:22.04`), `gcc-15-m32` (`ubuntu:26.04`): all seven configurations at Debug and the optimized step | optimized step: `default debug nochecks` × Release, MinSizeRel (`-m32`: `default` × Release) | CC-01..04 |
| 3 | `qemu / <arch>` | `ubuntu:26.04` | `aarch64`, `armhf`, `riscv64`, `powerpc`, `s390x` | 7 configuration steps at Debug (ctest under qemu); optimized step: `default nochecks` × MinSizeRel; symbol check | EM-01..04 |
| 3 | `bare / <family>` | `ubuntu:26.04` | `arm-gcc`, `clang` (thumbv6m, thumbv7m, rv32imac), `riscv-gcc`, `avr-gcc` | 6 configuration steps (`build_one.sh family:<f> <cfg>`: compile, symbols, static layout check, expected failures such as AVR `wide`); footprint step (`check_size.sh --family <f>`, summary + artifact) | XC-01..05, FP-01..05 |
| 3 | `lto / <cc>` | `ubuntu:26.04` | `gcc`, `clang` | steps `default`, `debug`, `nochecks` with `BLOC_LTO=ON` | LTO-01..02 |
| 3 | `fetch` | `ubuntu:26.04` | – | one step per offline variant of `fetchcontent_smoke.sh`; step `online` (FC-09) only for `push` and `schedule`, with the commit SHA | FC-01..10 |
| – | `ci-ok` | `ubuntu-latest` | – | fails if any needed job failed, was cancelled or was skipped | – |

That is 25 jobs per run. GCC coverage, sanitizers, LTO and the footprint baseline run only on the reference image, so their results do not depend on which older compiler happens to be installed.

### 10.3 Composite action `.github/actions/target`

Inputs (all strings, as composite actions require):

| Input | Meaning |
| --- | --- |
| `selector` | Target name or `family:<name>` from `scripts/ci/target_table.sh` |
| `packages` | apt packages for `apt_install.sh` |
| `cc` | Optional compiler override for host targets (`gcc`, `clang`); empty = table default |
| `configs` | Space-separated configurations to run at Debug; default: all seven |
| `optconfigs` | Space-separated configurations for the optimized step; empty = skip |
| `optbuildtypes` | Build types for the optimized step; default `Release MinSizeRel` |
| `symbols` | `'true'` runs the symbol check `check_no_heap.sh --target <selector>` (EM-03) |
| `size` | `'true'` runs the footprint step (bare-metal families) |

Steps: `apt_install.sh` (`id: install`) → `build_one.sh --versions <selector>` (prints every compiler and tool version) → seven configuration steps → optimized step (loops over `optbuildtypes` × `optconfigs`, continues after a failure and exits non-zero at the end) → symbol step when `symbols` is set → footprint step and artifact upload when `size` is set. A configuration step is selected with `contains(format(' {0} ', inputs.configs), ' <cfg> ')`, so `wide` cannot match inside another name. The calling job does the checkout and sets `container` and `timeout-minutes`.

### 10.4 Scripts behind the workflows

- `scripts/ci/target_table.sh` is the single source of truth for targets. `bloc_target <name>` sets `BLOC_KIND` (`host`, `emulated`, `baremetal`), `CC`, target flags, `BLOC_TOOLCHAIN_FILE`, `NM`, `SIZE`, `BLOC_EMULATOR`, the applicable configurations, expected failures (`avr-gcc:wide` → `BLOC_SIZE_T must not be wider than size_t`), `BLOC_EXPECT_BIG_ENDIAN` and `BLOC_EXPECT_PTR_BITS`. `bloc_family <name>` lists the targets of a family. Host targets are `host` (honours `CC`, default `gcc`, so CI selects the compiler per cell), and `host-gcc-m32`; the tier-1E targets are `aarch64`, `armhf`, `riscv64`, `powerpc`, `s390x`; bare-metal targets are named `<core>-<compiler>` (e.g. `cm0plus-gcc`, `rv32i-gcc`, `atmega328p-gcc`) and grouped into the families `arm-gcc`, `clang`, `riscv-gcc`, `avr-gcc`. Every other script reads targets only from here; there is no second list.
- `scripts/ci/build_one.sh <selector> <config> [Debug|Release|MinSizeRel] [--no-test] [--lto]` builds one combination. For `host` and `emulated` targets it configures CMake (`-G Ninja`, `CMAKE_BUILD_TYPE`, `BLOC_TEST_CONFIG`, toolchain file, `BLOC_EXPECT_*`) into `build/ci/<target>/<config>-<type>`, builds and runs `ctest --output-on-failure` (through the toolchain's emulator for tier 1E). For `baremetal` targets it runs the XC-01/02/05 checks for that configuration, or checks the expected failure message. A `family:` selector loops over the family's targets, continues after failures and exits non-zero if any failed. Every combination prints one line `PASS|FAIL|SKIP <target> <config> <type>`.
- `scripts/cross_check.sh`, `scripts/check_size.sh`, `scripts/coverage.sh`, `scripts/sanitize.sh`, `scripts/fetchcontent_smoke.sh` and `scripts/run_all.sh` are thin loops over these two scripts and the tools of section 9.
- All scripts use `#!/usr/bin/env bash` and `set -euo pipefail`, pass `shellcheck`, and work from any working directory (they `cd` to the repository root).

---

## 11. Traceability (spec section → tests)

| Spec section | Tests |
| --- | --- |
| 2 No-heap, dependencies | NH-01..03, XC-01..03 |
| 3 Layout, alignment | CFG-03..05, CFG-09, ALLOC-02, ALLOC-15, XC-05, CC-02, EM-01, EM-04 |
| 4 Pool size, storage | CFG-01, CFG-02, CFG-07, CFG-09, POOL-06..10, LTO-01 (storage aliasing) |
| 5 Handle, data model | CFG-05, CFG-10, ACC-01 |
| 6 Configuration | CFG-06, CFG-08, CF-01..10, XC-04, FC-03, FC-04, LTO-02 |
| 7 Pool lifecycle | POOL-01..23, XC-02 (no division in init) |
| 8 Allocation | ALLOC-01..17 |
| 9 Reference counting, sharing | REF-01..11, DBG-09, LEN-08 |
| 10 Accessors, length ops | ACC-01..04, LEN-01..09, PRE-09 |
| 11 Copy, append, prepend | CPY-01..12, APP-01..07, PRE-01..08, DBG-08 |
| 12 Thread safety | TS-01..04, POOL-22, ALLOC-16, REF-11, DBG-12 |
| 13 Errors, validation | DBG-01..13, all CHK tests, CF-08, CF-11 |
| 14 Invariants | MODEL-01..02, ALLOC-17, DBG-10 |
| 15 API | CFG-10, CFG-11, CF-10 |
| 17 Code size | FP-01..06, XC-02, CC-03 |
| – (plan R-10, section 3.8: CMake, consumption) | CM-01..10 via FC-01..10 |
| – (plan Q-08: compilers and architectures) | CC-01..04, EM-01..04, XC-01..05 |

---

## 12. Phases

Each phase implements code **and** all tests for that code, including the DBG and TS aspects of the functions it adds. The DoD of every phase from phase 2 on includes: zero warnings with every compiler of section 3.6, all tests passing in all seven configurations with GCC and Clang and on every tier-1E emulated target, the FetchContent variants scheduled so far green, `scripts/cross_check.sh` green, `scripts/check_size.sh` report generated (informational until phase 4), `scripts/coverage.sh` at 100 % for all code that exists so far in all six coverage configurations, guard bands and lock balance clean. Debug paths are not deferred: a function is only done when its debug branches are covered too.

### Phase 0 — Scaffolding

Work:
- Directory layout (section 2), top-level and test `CMakeLists.txt`, `CMakePresets.json` with the presets of section 3.2 for GCC and Clang and all seven configurations. The top-level `CMakeLists.txt` follows section 3.8 from the first commit (top-level detection, options, PUBLIC/PRIVATE split, config-header mechanism); `BLOC_TEST_CONFIG` is implemented through `BLOC_CONFIG_HEADER`/`BLOC_CONFIG_DIRS`.
- Unity via `FetchContent` (v2.6.1), inside `test/CMakeLists.txt` only.
- `cmake/toolchains/linux-<arch>.cmake` for the five tier-1E targets; `scripts/ci/target_table.sh`, `scripts/ci/build_one.sh`, `scripts/ci/apt_install.sh` (section 10.4); `test/test_platform.c` (EM-04).
- `test/fetchcontent_smoke/` with the full `CMakeLists.txt` (FetchContent block and FC-02 assertions), `config/smoke_opts.h`, the bare-metal toolchain file, and a `main.c` that for now only uses compile-time macros (e.g. `_Static_assert(BLOC_POOL_SIZE(4, 64) > 0, "")`) and returns 0; `scripts/fetchcontent_smoke.sh` with all variants.
- Test support library (section 5) complete, with its own self-tests (`test_support.c`: guard band detection, assertion bookkeeping, lock tracer errors).
- All seven `test/configs/cfg_*.h`.
- Stub `src/include/bloc.h`, `src/include/bloc_opt.h`, `src/bloc.c` and `src/CMakeLists.txt`. An empty translation unit is not valid ISO C under `-Wpedantic`, so the stub `bloc.c` contains one internal declaration, e.g. `typedef int bloc_i_translation_unit_not_empty;`.
- `scripts/*.sh` skeletons, including `cross_check.sh` (loop over the bare-metal targets of `target_table.sh`) and `check_size.sh` (report only). Also `.clang-format`, `.github/workflows/ci.yml` and `.github/actions/target/action.yml` with every job of section 10.2. Jobs whose tests do not exist yet run but are informational (not in `ci-ok.needs`) until their phase. The footprint step is informational until phase 4.

DoD: `ctest` runs `test_support` green in all configurations with GCC and Clang, on the 32-bit host and on every tier-1E target (EM-04 passes, so each emulated job runs the right architecture); `cross_check.sh` compiles the stub `bloc.c` for every tier-2 target; FC-02, FC-06, FC-07 and FC-08 pass with the macro-only `main.c`; the CI pipeline runs end to end, and `ci-ok` is green.

Commit: `phase 0: project scaffolding, test support and CI`

### Phase 1 — Public headers and compile-time validation

Work: complete `bloc_opt.h` and `bloc.h` (sections 4.1 and 4.2). `bloc.c` still has no function bodies; only `test_support.c`, `test_layout.c` and the compile-fail tests are built. The coverage gate does not apply yet because there is no executable library code.

Tests: CFG-01..11, CF-01..11, XC-04, XC-05.

DoD: all CFG and CF tests pass in all configurations with GCC and Clang; XC-04 and XC-05 pass for every tier-2 target; zero warnings.

Commit: `phase 1: public headers, layout macros, compile-time validation`

### Phase 2 — Core: pool, allocation, accessors, reference counting

These functions depend on each other for full branch coverage (e.g. `BLOC_BUSY` needs an allocated block, LIFO reuse needs release), so they form one phase.

Work: internal macros and helpers (sections 4.3, 4.4); `bloc_pool_init`, `bloc_pool_deinit`, `bloc_pool_free_count`, `bloc_pool_get_stats`, `bloc_alloc`, `bloc_calloc`, `bloc_data`, `bloc_len`, `bloc_headroom`, `bloc_tailroom`, `bloc_retain`, `bloc_release`. Suggested order inside the phase: pool lifecycle → alloc → accessors → retain/release → calloc.

Tests: POOL-01..23, ALLOC-01..17, ACC-01 (alloc part), ACC-02..04, REF-01..11; DBG-01..07 and DBG-10 rows for these functions; TS-01 rows for these functions. Smoke `main.c` gains checks 1, 2, 4 and 5 of section 9.8: FC-01, FC-03, FC-04 and FC-05 pass. `coverage`, `no-heap` and `baremetal` join `ci-ok`.

Commit: `phase 2: pool lifecycle, allocation, accessors, retain and release`

### Phase 3 — Zero-copy length operations

Work: `bloc_set_len`, `bloc_add_header`, `bloc_remove_header`.

Tests: LEN-01..09, ACC-01 completion for these operations; DBG-01..06 and DBG-09 rows for these functions; TS-01 rows.

Commit: `phase 3: set_len, add_header, remove_header`

### Phase 4 — Copy, append, prepend

Work: `bloc_copy_from`, `bloc_copy_to`, `bloc_copy`, `bloc_append`, `bloc_append_data`, `bloc_prepend`, `bloc_prepend_data`.

Tests: CPY-01..12, APP-01..07, PRE-01..09, ACC-01 completion; DBG-01..06, DBG-08 and DBG-09 rows for these functions; TS-01 rows. With the API complete: FP-01..03 and FP-05. Smoke `main.c` gains check 3 of section 9.8 (append, add_header, copy_to).

Additional DoD: the spec budgets (FP-02) are met. A size-reduction pass is done (section 4.6), comparing per-function sizes on ARMv6-M and ARMv7-M, before `scripts/size_baseline.txt` is created from the measured values. If a budget cannot be met without violating another rule, record it in `docs/OPEN_QUESTIONS.md` (section 1.3) instead of committing.

Commit: `phase 4: copy, append and prepend`

### Phase 5 — Cross-cutting suites and model test

Work: no new API. Complete the cross-cutting tests and fix any gaps they reveal; implement the reference model and the pthread stress test.

Tests: DBG-11..13, TS-02..04, MODEL-01..02; LTO-01..02. `sanitize` and `lto` join `ci-ok`.

Commit: `phase 5: cross-cutting debug, thread-safety and model-based tests`

### Phase 6 — Hardening and enforced gates

Work: complete `scripts/sanitize.sh` and `scripts/check_no_heap.sh`, the `-O2`/`-Os` host builds (CC-03), the FP-04 baseline gate, and the EM-03 symbol check. Every job of section 10.2 is now required (listed in `ci-ok.needs`). Enable branch protection on `main` with `ci-ok` as the only required check (the repository owner does this; note it in the commit message). Confirm Q-02: `grep -rnE "LCOV_EXCL|GCOVR_EXCL" src include test` returns nothing.

Tests: SAN-01..03, NH-01..03, CC-01..04, XC-01..05, FP-01..06, EM-01..04, FC-01..10.

DoD: the full CI pipeline is green with all gates blocking.

Commit: `phase 6: sanitizers, no-heap, cross-compiler and footprint gates`

### Phase 7 — Documentation and examples

Work:
- `README.md`: purpose, feature list, the no-heap guarantee, supported compilers and targets (section 3.6), a footprint table from `build/size/report.md`, quick start (pool storage, init, alloc with headroom, append, add_header, release), **"Using BLOC in your CMake project"** with the `bloc-fetchcontent` block of section 3.8, the `BLOC_CONFIG_HEADER`/`BLOC_CONFIG_DIRS` options and the warning of CM-06 (never set the config header on your own target only), configuration table (link to spec section 6), how to build and run tests, coverage and sanitizer scripts, a CI badge, license.
- FC-10 enabled in `fetchcontent_smoke.sh` and in the `fetchcontent` job.
- `examples/basic.c` and `examples/bloc_opts_example.h`, built in CI with the `default` configuration.
- Doxygen comments complete for every public symbol.

Commit: `phase 7: README, examples and API documentation`

---

## 13. Definition of Done (project)

- [ ] All 22 public functions implemented as specified in `docs/BLOC_SPEC.md` revision 4.
- [ ] Every test ID in section 8 exists as a test function and passes in every configuration where its tag applies.
- [ ] 100 % line, branch and function coverage of `src/bloc.c` in each of the six coverage configurations, without exclusion markers.
- [ ] Zero warnings with every compiler and target of section 3.6; clang-format clean.
- [ ] Cross-compile matrix XC-01..05 green.
- [ ] Footprint within the spec section 17 budgets on ARMv6-M and ARMv7-M; FP-01..06 green; `scripts/size_baseline.txt` committed.
- [ ] ASan/UBSan and TSan runs clean.
- [ ] No-heap check NH-01..03 green; the library references only `memcpy` and `memset`.
- [ ] All tier-1 host compilers (CC-01..04) and tier-1E emulated targets (EM-01..04) green; LTO-01..02 green.
- [ ] CMake rules CM-01..10 implemented; FetchContent smoke tests FC-01..10 green, including the CMake 3.20 floor and the bare-metal consumer.
- [ ] CI pipeline (section 10) green on `main`, with `ci-ok` as the required check and every job listed in it.
- [ ] `docs/OPEN_QUESTIONS.md` either absent or every entry has a chosen interpretation and linked tests.
- [ ] README and examples complete.

---

## Appendix A — CI workflow files

The skeletons that stood here were replaced by the real files: `.github/workflows/ci.yml` and `.github/actions/target/action.yml`. Section 10 states the structure and rules they follow.
