# BLOC V1 — Implementation Plan

Plan revision 1 · 2026-10-03 · for `docs/BLOC_SPEC.md` revision 3

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
| R-02 | `src/` and `include/` may include only `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`, `<string.h>`. Never `<stdlib.h>`, `<stdio.h>`, `<assert.h>`. |
| R-03 | The only C library symbols the compiled library may reference are `memcpy` and `memset`. |
| R-04 | Pure C11 (`-std=c11`), no compiler extensions except `__builtin_trap` inside the guarded default of `BLOC_PLATFORM_ASSERT`. |
| R-05 | No recursion, no floating point, no function-local `static` variables, no global mutable state in `src/`. |
| R-06 | No `static inline` functions in public headers. All executable code lives in `src/bloc.c`, so coverage measures it. |
| R-07 | Do not add, remove or rename public API functions, types, macros or enum values beyond spec section 15 and the macros named in the spec. |
| R-08 | Every behaviour must be reachable from the public API or from a documented white-box test technique (section 6.4). No test-only code paths in `src/` (no `#ifdef TESTING`, no `#ifdef COVERAGE`). |

### 1.2 Quality rules

| ID | Rule |
| --- | --- |
| Q-01 | 100 % line, 100 % branch and 100 % function coverage of `src/bloc.c`, separately for **every** coverage configuration in section 6.2. |
| Q-02 | Coverage exclusion markers are forbidden: no `LCOV_EXCL_*`, `GCOVR_EXCL_*`, `// NOSONAR`-style or pragma-based exclusions. The only permitted gcovr option that hides branches is `--exclude-unreachable-branches`. |
| Q-03 | If a branch cannot be covered, the code is wrong: restructure it (remove a defensive branch that the spec does not require, merge conditions, reorder checks). Never lower thresholds. |
| Q-04 | Zero compiler warnings with GCC and Clang at the flags in section 3.3 (`-Werror`). |
| Q-05 | All tests pass under AddressSanitizer + UndefinedBehaviorSanitizer and the thread stress test passes under ThreadSanitizer. |
| Q-06 | Tests never hard-code layout numbers (4, 12, 16 …). They derive expected values from the public macros and the formulas in the spec, so the same test is valid in every configuration. |

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
├── CMakeLists.txt                 top-level build (library, tests, options)
├── CMakePresets.json              one configure preset per configuration (section 6)
├── README.md                      overview, quick start, build/test instructions (phase 10)
├── LICENSE                        existing, MIT
├── .clang-format                  formatting rules (section 3.5)
├── .github/workflows/ci.yml       CI pipeline (section 10)
├── docs/
│   ├── BLOC_SPEC.md               specification (existing)
│   ├── IMPLEMENTATION_PLAN.md     this file
│   └── OPEN_QUESTIONS.md          only if needed (section 1.3)
├── include/
│   ├── bloc.h                     public API, types, layout macros
│   └── bloc_opt.h                 configuration defaults and compile-time validation
├── src/
│   └── bloc.c                     the whole implementation
├── examples/
│   ├── basic.c                    alloc / append / prepend / release example
│   └── bloc_opts_example.h        example project configuration header
├── scripts/
│   ├── coverage.sh                build + test + gcovr gate for every coverage config
│   ├── sanitize.sh                ASan/UBSan run over every coverage config
│   ├── check_no_heap.sh           symbol check for R-01..R-03
│   └── run_all.sh                 everything CI does, locally
└── test/
    ├── CMakeLists.txt
    ├── configs/                   one header per test configuration (section 6)
    ├── support/                   test support library (section 5)
    ├── compile_fail/              sources that must fail to compile (section 8.1)
    └── test_*.c                   Unity test files (section 8)
```

---

## 3. Toolchain and build system

### 3.1 Tools

| Tool | Version | Use |
| --- | --- | --- |
| CMake | ≥ 3.20 | Build, CTest |
| GCC | ≥ 11 | Primary compiler, coverage (`gcov`) |
| Clang | ≥ 14 | Second compiler, sanitizers |
| gcovr | ≥ 7.0 | Coverage report and gate (`--fail-under-function` needs ≥ 7) |
| Unity | v2.6.1 (tag), fetched with CMake `FetchContent` | Unit test framework |
| binutils `nm` | any | No-heap symbol check |
| `arm-none-eabi-gcc` | optional | Cross-compile check (section 9.4) |

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

| Option | Default | Meaning |
| --- | --- | --- |
| `BLOC_BUILD_TESTS` | `ON` when top-level project | Build tests and register them with CTest |
| `BLOC_BUILD_EXAMPLES` | `ON` when top-level project | Build `examples/` |
| `BLOC_TEST_CONFIG` | `default` | One of `default`, `debug`, `nochecks`, `wide`, `noalign`, `bigalign`, `pthread` |
| `BLOC_COVERAGE` | `OFF` | Adds `-O0 -g --coverage` to library and tests (GCC only) |
| `BLOC_SANITIZE` | empty | Comma list passed to `-fsanitize=`, e.g. `address,undefined` or `thread` |

`BLOC_TEST_CONFIG` selects `test/configs/cfg_<name>.h`. For every value except `default`, the build adds `BLOC_CONFIG_HEADER="cfg_<name>.h"` (quoted string) and `test/configs` plus `test/support` to the include path **of both the library and the tests**, so library and tests are always compiled with the same configuration. For `default`, `BLOC_CONFIG_HEADER` is not defined at all; this proves the library builds with zero configuration.

Targets:

- `bloc` — static library from `src/bloc.c`, public include dir `include/`, alias `bloc::bloc`.
- `bloc_test_support` — static library from `test/support/*.c`.
- one executable per `test/test_*.c`, each linking `bloc`, `bloc_test_support` and `unity`, each registered with `add_test`.
- `compile_fail_*` targets (section 8.1).

### 3.3 Compiler flags

Library (`bloc`), both compilers:

```text
-std=c11 -Wall -Wextra -Wpedantic -Werror
-Wconversion -Wsign-conversion -Wshadow -Wundef -Wvla -Wpointer-arith
-Wstrict-prototypes -Wmissing-prototypes -Wcast-align -fno-common
```

GCC additionally: `-Wcast-align=strict` (replaces `-Wcast-align`). Release builds: `-O2`. Coverage builds: `-O0 -g --coverage`.

Tests: `-std=c11 -Wall -Wextra -Wpedantic -Werror` (no `-Wconversion`). Unity itself is compiled without `-Werror`.

`-Wcast-qual` is intentionally not enabled: `bloc_data()` is the single sanctioned place that removes `const` (spec section 10); mark it with a comment.

### 3.4 Casting rules that keep `-Wcast-align=strict` quiet

- Byte pointer to handle: `(struct bloc_handle *)(void *)p`.
- Handle to byte pointer: `(uint8_t *)(void *)h`.
- Address comparisons for debug checks: convert to `uintptr_t` first.

### 3.5 Style

- `.clang-format` based on LLVM, `IndentWidth: 4`, `ColumnLimit: 100`, `BreakBeforeBraces: Linux`.
- Public symbols prefixed `bloc_` / `BLOC_`; internal `static` helpers prefixed `bloc_i_`.
- Every public function has a Doxygen comment in `bloc.h`: brief, parameters, return values (every status code it can return), thread-safety note.
- Use `0u`, `1u` for unsigned literals; explicit casts when narrowing (`b->offset = (bloc_size_t)(b->offset - n);`).

---

## 4. Code architecture

### 4.1 `include/bloc_opt.h`

Contents, in this order:

1. Include guard.
2. Default for every option in spec section 6, each wrapped in `#ifndef`. Defaults: `BLOC_BLOCK_ALIGNMENT 4`, `BLOC_PAYLOAD_ALIGNMENT 4`, `BLOC_SIZE_T uint16_t`, `BLOC_COUNT_T uint8_t`, `BLOC_REFCOUNT_T uint8_t`, `BLOC_THREAD_SAFE 0`, `BLOC_CHECKS 1`, `BLOC_DEBUG 0`, `BLOC_STATS 0`.
3. Default `BLOC_PLATFORM_ASSERT(msg)` exactly as in spec section 6 (trap, no libc).
4. Preprocessor validation with `#error` and the exact messages from spec section 6:
   - `#if BLOC_DEBUG && !BLOC_CHECKS` → `#error "BLOC_DEBUG requires BLOC_CHECKS"`.
   - `#if BLOC_THREAD_SAFE && !(defined(BLOC_DECL_PROTECT) && defined(BLOC_PROTECT) && defined(BLOC_UNPROTECT))` → `#error "BLOC_THREAD_SAFE requires BLOC_DECL_PROTECT, BLOC_PROTECT and BLOC_UNPROTECT"`.
5. `_Static_assert`s for the other rules in spec section 6 with the exact messages. Use a helper `#define BLOC_IS_POW2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))`.

### 4.2 `include/bloc.h`

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
| `static bool bloc_i_addr_valid(const struct bloc_handle *b)` | `BLOC_DEBUG` | Handle validity steps 2–5 (spec section 13), one `if` per step |
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
REQUIRE storage_size / stride >= element_count                         → BLOC_BOUNDS
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

All test configurations except `default` route `BLOC_PLATFORM_ASSERT(msg)` to `ts_assert_fail(msg)`. The `pthread` configuration defines the protect macros around one global `pthread_mutex_t` from `test/support/ts_pthread.c`.

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

Compile-fail tests: each is a tiny `.c` file plus a config header; the CMake target is `EXCLUDE_FROM_ALL`, and a CTest test builds it (`cmake --build . --target <cf>`) and passes only if the build output matches the given regex (`PASS_REGULAR_EXPRESSION`), so a failure for an unrelated reason is caught. Compile-fail targets use `-std=c11 -Werror` without `-Wpedantic`. They run in every build directory.

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
| NH-01 | Build the library in release mode (`-O2`, no coverage, no sanitizers, `-fno-stack-protector`) for configurations `default` and `debug`. Run `nm -u` on the object file. The set of undefined symbols must be a subset of `{memcpy, memset}` (plus the test hooks `ts_assert_fail`, `ts_lock_enter`, `ts_lock_exit` in `debug`). Any other symbol fails the check, and the script prints it. |
| NH-02 | Same check for a `debug` build that uses the **default** `BLOC_PLATFORM_ASSERT` (no test hook): undefined symbols ⊆ `{memcpy, memset}`. This proves the trapping default needs no C library. |
| NH-03 | `grep` over `src/` and `include/` finds no `#include` other than `bloc.h`, `bloc_opt.h`, `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`, `<string.h>`, and no occurrence of `malloc`, `calloc`, `realloc`, `aligned_alloc`, `free(`, `memmove`, `alloca` (not even in comments, so the check stays a plain grep; `bloc_calloc` is matched as a whole word and allowed). |

### 9.2 Sanitizers (`scripts/sanitize.sh`)

| ID | Requirement |
| --- | --- |
| SAN-01 | Every coverage-gated configuration built with Clang and `-fsanitize=address,undefined -fno-sanitize-recover=all`; all tests pass with zero reports. |
| SAN-02 | The `pthread` configuration built with `-fsanitize=thread`; TS-04 passes with zero reports. |

### 9.3 Compilers

| ID | Requirement |
| --- | --- |
| CC-01 | All configurations build and pass with GCC and with Clang at the flags of section 3.3. |
| CC-02 | Optional 32-bit job (`-m32`, needs `gcc-multilib`): `default` configuration passes; `sizeof(struct bloc_handle) == 12` there. |

### 9.4 Embedded cross-compile (optional, recommended)

| ID | Requirement |
| --- | --- |
| XC-01 | `arm-none-eabi-gcc -mcpu=cortex-m0 -mthumb -Os -std=c11` compiles `src/bloc.c` in `default` and `debug` (default assert) configurations without warnings; NH-01 symbol check passes on the object; `arm-none-eabi-size` output is printed in CI. |

---

## 10. Continuous integration (`.github/workflows/ci.yml`)

Runner: `ubuntu-24.04`. Install `cmake`, `gcc`, `clang`, `python3-pip`; `pip install gcovr`. Jobs:

| Job | Matrix | Steps |
| --- | --- | --- |
| `build-test` | compiler {gcc, clang} × config {all 7} | configure, build, `ctest` |
| `coverage` | config {6 coverage-gated} | `scripts/coverage.sh <cfg>`; upload `coverage.html` as artifact |
| `sanitize` | – | `scripts/sanitize.sh` (SAN-01, SAN-02) |
| `no-heap` | – | `scripts/check_no_heap.sh` (NH-01..03) |
| `cross` | – | install `gcc-arm-none-eabi`; XC-01 (allowed to be skipped if the package is unavailable, but not to fail) |
| `format` | – | `clang-format --dry-run --Werror` over `include/ src/ test/ examples/` |

All jobs except `cross` are required. `scripts/run_all.sh` runs the same steps locally.

---

## 11. Traceability (spec section → tests)

| Spec section | Tests |
| --- | --- |
| 2 No-heap, dependencies | NH-01..03, XC-01 |
| 3 Layout, alignment | CFG-03..05, CFG-09, ALLOC-02, ALLOC-15 |
| 4 Pool size, storage | CFG-01, CFG-02, CFG-07, CFG-09, POOL-06..10 |
| 5 Handle, data model | CFG-05, CFG-10, ACC-01 |
| 6 Configuration | CFG-06, CFG-08, CF-01..10 |
| 7 Pool lifecycle | POOL-01..23 |
| 8 Allocation | ALLOC-01..17 |
| 9 Reference counting, sharing | REF-01..11, DBG-09, LEN-08 |
| 10 Accessors, length ops | ACC-01..04, LEN-01..09, PRE-09 |
| 11 Copy, append, prepend | CPY-01..12, APP-01..07, PRE-01..08, DBG-08 |
| 12 Thread safety | TS-01..04, POOL-22, ALLOC-16, REF-11, DBG-12 |
| 13 Errors, validation | DBG-01..13, all CHK tests, CF-08, CF-11 |
| 14 Invariants | MODEL-01..02, ALLOC-17, DBG-10 |
| 15 API | CFG-10, CFG-11, CF-10 |

---

## 12. Phases

Each phase implements code **and** all tests for that code, including the DBG and TS aspects of the functions it adds. The DoD of every phase from phase 2 on includes: zero warnings with GCC and Clang, all tests passing in all seven configurations, `scripts/coverage.sh` at 100 % for all code that exists so far in all six coverage configurations, guard bands and lock balance clean. Debug paths are not deferred: a function is only done when its debug branches are covered too.

### Phase 0 — Scaffolding

Work:
- Directory layout (section 2), top-level and test `CMakeLists.txt`, `CMakePresets.json` with presets `dev-<cfg>` (Debug, GCC) and `cov-<cfg>` (coverage) for all seven configurations.
- Unity via `FetchContent` (v2.6.1).
- Test support library (section 5) complete, with its own self-tests (`test_support.c`: guard band detection, assertion bookkeeping, lock tracer errors).
- All seven `test/configs/cfg_*.h`.
- Stub `include/bloc.h`, `include/bloc_opt.h` and `src/bloc.c`. An empty translation unit is not valid ISO C under `-Wpedantic`, so the stub `bloc.c` contains one internal declaration, e.g. `typedef int bloc_i_translation_unit_not_empty;`.
- `scripts/*.sh` skeletons, `.clang-format`, CI workflow with the jobs of section 10 (coverage and no-heap jobs informational until phase 2).

DoD: `ctest` runs `test_support` green in all configurations; CI pipeline runs.

Commit: `phase 0: project scaffolding, test support and CI`

### Phase 1 — Public headers and compile-time validation

Work: complete `bloc_opt.h` and `bloc.h` (sections 4.1 and 4.2). `bloc.c` still has no function bodies; only `test_support.c`, `test_layout.c` and the compile-fail tests are built. The coverage gate does not apply yet because there is no executable library code.

Tests: CFG-01..11, CF-01..11.

DoD: all CFG and CF tests pass in all configurations, zero warnings.

Commit: `phase 1: public headers, layout macros, compile-time validation`

### Phase 2 — Core: pool, allocation, accessors, reference counting

These functions depend on each other for full branch coverage (e.g. `BLOC_BUSY` needs an allocated block, LIFO reuse needs release), so they form one phase.

Work: internal macros and helpers (sections 4.3, 4.4); `bloc_pool_init`, `bloc_pool_deinit`, `bloc_pool_free_count`, `bloc_pool_get_stats`, `bloc_alloc`, `bloc_calloc`, `bloc_data`, `bloc_len`, `bloc_headroom`, `bloc_tailroom`, `bloc_retain`, `bloc_release`. Suggested order inside the phase: pool lifecycle → alloc → accessors → retain/release → calloc.

Tests: POOL-01..23, ALLOC-01..17, ACC-01 (alloc part), ACC-02..04, REF-01..11; DBG-01..07 and DBG-10 rows for these functions; TS-01 rows for these functions.

Commit: `phase 2: pool lifecycle, allocation, accessors, retain and release`

### Phase 3 — Zero-copy length operations

Work: `bloc_set_len`, `bloc_add_header`, `bloc_remove_header`.

Tests: LEN-01..09, ACC-01 completion for these operations; DBG-01..06 and DBG-09 rows for these functions; TS-01 rows.

Commit: `phase 3: set_len, add_header, remove_header`

### Phase 4 — Copy, append, prepend

Work: `bloc_copy_from`, `bloc_copy_to`, `bloc_copy`, `bloc_append`, `bloc_append_data`, `bloc_prepend`, `bloc_prepend_data`.

Tests: CPY-01..12, APP-01..07, PRE-01..09, ACC-01 completion; DBG-01..06, DBG-08 and DBG-09 rows for these functions; TS-01 rows.

Commit: `phase 4: copy, append and prepend`

### Phase 5 — Cross-cutting suites and model test

Work: no new API. Complete the cross-cutting tests and fix any gaps they reveal; implement the reference model and the pthread stress test.

Tests: DBG-11..13, TS-02..04, MODEL-01..02.

Commit: `phase 5: cross-cutting debug, thread-safety and model-based tests`

### Phase 6 — Hardening and enforced gates

Work: `scripts/sanitize.sh`, `scripts/check_no_heap.sh`, optional cross-compile job; make the CI `coverage`, `sanitize` and `no-heap` jobs required and blocking. Confirm Q-02: `grep -rnE "LCOV_EXCL|GCOVR_EXCL" src include test` returns nothing.

Tests: SAN-01..02, NH-01..03, CC-01..02, XC-01.

DoD: the full CI pipeline is green with all gates blocking.

Commit: `phase 6: sanitizers, no-heap verification and enforced CI gates`

### Phase 7 — Documentation and examples

Work:
- `README.md`: purpose, feature list, the no-heap guarantee, quick start (pool storage, init, alloc with headroom, append, add_header, release), configuration table (link to spec section 6), how to build and run tests, coverage and sanitizer scripts, license.
- `examples/basic.c` and `examples/bloc_opts_example.h`, built in CI with the `default` configuration.
- Doxygen comments complete for every public symbol.

Commit: `phase 7: README, examples and API documentation`

---

## 13. Definition of Done (project)

- [ ] All 22 public functions implemented as specified in `docs/BLOC_SPEC.md` revision 3.
- [ ] Every test ID in section 8 exists as a test function and passes in every configuration where its tag applies.
- [ ] 100 % line, branch and function coverage of `src/bloc.c` in each of the six coverage configurations, without exclusion markers.
- [ ] Zero warnings with GCC and Clang; clang-format clean.
- [ ] ASan/UBSan and TSan runs clean.
- [ ] No-heap check NH-01..03 green; the library references only `memcpy` and `memset`.
- [ ] CI pipeline green on the default branch.
- [ ] `docs/OPEN_QUESTIONS.md` either absent or every entry has a chosen interpretation and linked tests.
- [ ] README and examples complete.
