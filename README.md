# BLOC

[![CI](https://github.com/Andste82/bloc/actions/workflows/ci.yml/badge.svg)](https://github.com/Andste82/bloc/actions/workflows/ci.yml)

**Buffer Lifetime & Offset Control**: deterministic, heap-free, fixed-block buffers with reference
counting and zero-copy headroom, written in C11 for embedded systems and host programs.

BLOC manages a fixed number of equally sized buffers in storage that you provide. A buffer is a
small handle embedded in its block. The payload is a window (offset, length) into the block's
data area, so protocol headers are added and removed without copying. It borrows the proven parts
of lwIP `pbuf` (pool allocation, headroom, `add_header` and `remove_header`, reference counting)
and leaves out chaining and buffer types.

## Features

- Fixed-size blocks in caller-provided, statically allocatable storage.
- O(1) allocation and release through an intrusive free list that costs no extra memory.
- Reference counting with overflow protection (the count never wraps).
- Headroom, `bloc_add_header`, `bloc_remove_header` and `bloc_set_len` without copying.
- Copy, append and prepend between buffers and external memory, including self-append.
- Compile-time block and payload alignment, and compile-time integer types for all fields.
- Optional locking through application-supplied macros, no OS dependency.
- Optional parameter checks, debug assertions and pool statistics, all removed when disabled.
- Small: the whole API needs 860 bytes of `.text` on ARMv6-M (see Footprint).

## The no-heap guarantee

BLOC never allocates memory. It does not call `malloc`, `calloc`, `realloc` or `free`, keeps no
global state and has no hidden dependencies. All storage comes from the caller. The only external
symbols the library object file references are `memcpy` and `memset`. `scripts/check_no_heap.sh`
enforces this on every build: a grep of the sources and a symbol check of the compiled objects on
the host, the emulated targets and the bare-metal targets.

## Supported compilers and targets

| Tier | Compiler | Targets |
| --- | --- | --- |
| Host | GCC 11 to 15, Clang 14 to 21 (Ubuntu 22.04, 24.04, 26.04) | x86-64, and i386 with GCC `-m32` |
| Emulated (tests run under `qemu-user`) | Linux cross GCC, static | aarch64, armhf, riscv64, powerpc (big-endian), s390x (big-endian) |
| Bare metal (compile, layout, symbol and size checks) | `arm-none-eabi-gcc`, Clang, `riscv64-unknown-elf-gcc`, `avr-gcc` | Cortex-M0+, M3, M4, A7, R5, RV32I, RV32IMAC, RV64IMAC, AVR ATmega328P |

macOS and Windows are not supported or tested in V1.

## Footprint

`.text` of the complete API in bytes, measured by `scripts/check_size.sh` with `arm-none-eabi-gcc`,
`-Os -mthumb -ffunction-sections -fdata-sections -std=c11`, no LTO. `.rodata`, `.data` and `.bss`
are 0 in these configurations. The budgets come from section 17 of the specification.

| Configuration | Cortex-M0+ (ARMv6-M) | budget | Cortex-M3 (ARMv7-M) | budget |
| --- | ---: | ---: | ---: | ---: |
| `default` (checks on) | 860 | 1536 | 812 | 1280 |
| `nochecks` (`BLOC_CHECKS` 0) | 626 | 1152 | 618 | 960 |

The full table for every target and configuration is written to `build/size/report.md`.

## Quick start

```c
#include "bloc.h"

#define PACKET_COUNT 8u
#define PACKET_SIZE  128u

/* 1. Storage: suitably aligned, sized for PACKET_COUNT buffers of PACKET_SIZE data bytes. */
static BLOC_POOL_STORAGE(storage, PACKET_COUNT, PACKET_SIZE);
static bloc_pool_t pool;

void app_init(void)
{
    /* 2. Initialize the pool once at start-up and check the result. */
    if (bloc_pool_init(&pool, storage, sizeof(storage), PACKET_COUNT, PACKET_SIZE) != BLOC_OK) {
        /* wrong alignment or size: a programming error */
    }
}

void send_message(const void *msg, bloc_size_t msg_len)
{
    /* 3. Allocate with 8 bytes of headroom for a header that is added later. */
    bloc_handle_t b = bloc_alloc(&pool, 8u);
    if (b == NULL) {
        return; /* pool empty */
    }

    /* 4. Append the payload behind the headroom. */
    if (bloc_append_data(b, msg, msg_len) == BLOC_OK) {
        /* 5. Expose 4 bytes of headroom as payload and write the header in place. */
        if (bloc_add_header(b, 4u) == BLOC_OK) {
            ((uint8_t *)bloc_data(b))[0] = 0x42u;
            /* hand bloc_data(b) and bloc_len(b) to the driver ... */
        }
    }

    /* 6. Release: the block returns to the pool when the last reference is dropped. */
    bloc_release(b);
}
```

`bloc_retain()` adds a reference
for a second owner; every owner calls `bloc_release()` exactly once and sets its handle to `NULL`
afterwards. A complete, runnable program is in [`examples/basic.c`](examples/basic.c), and a
sample project configuration header is in
[`examples/bloc_opts_example.h`](examples/bloc_opts_example.h).

Buffers with more than one reference are read-only: the functions that change a buffer
(`bloc_set_len`, `bloc_add_header`, `bloc_remove_header`, `bloc_copy_from`, `bloc_append*`,
`bloc_prepend*`) require a reference count of 1. The API is documented with Doxygen comments in
[`src/include/bloc.h`](src/include/bloc.h); the behaviour is specified in
[`docs/BLOC_SPEC.md`](docs/BLOC_SPEC.md).

## Using BLOC in your CMake project

BLOC builds one library target, `bloc::bloc`, and has no side effects on your project. With
CMake 3.20 or newer:

```cmake
# >>> bloc-fetchcontent  (this block is kept identical in README.md and test/fetchcontent_smoke/CMakeLists.txt, FC-10)
include(FetchContent)
FetchContent_Declare(bloc
  GIT_REPOSITORY https://github.com/Andste82/bloc.git
  GIT_TAG        v1.0.0   # a release tag such as v1.0.0, or a full commit hash
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  src)               # the library only: no project, no tests
# optional project configuration (spec section 6):
# set(BLOC_CONFIG_HEADER "bloc_opts.h")
# set(BLOC_CONFIG_DIRS   "${CMAKE_CURRENT_SOURCE_DIR}/config")
FetchContent_MakeAvailable(bloc)
# <<< bloc-fetchcontent

target_link_libraries(my_app PRIVATE bloc::bloc)
```

`add_subdirectory(path/to/bloc/src bloc)` works the same way; `src/` holds the whole library. To use a local checkout without network
access, pass `-DFETCHCONTENT_SOURCE_DIR_BLOC=/path/to/bloc` when configuring your project.

Project options (set them before `FetchContent_MakeAvailable`):

| Option | Default | Meaning |
| --- | --- | --- |
| `BLOC_CONFIG_HEADER` | empty | Name of your configuration header, for example `bloc_opts.h`. It becomes the PUBLIC compile definition `BLOC_CONFIG_HEADER="<value>"` of `bloc`. |
| `BLOC_CONFIG_DIRS` | empty | Absolute include directories (a `;`-list) that contain the configuration header. A relative path is a configure error. |
| `BLOC_BUILD_TESTS` | `ON` for a top-level build, else `OFF` | Build and register the tests (only possible when BLOC is the top-level project). |
| `BLOC_BUILD_EXAMPLES` | `ON` for a top-level build, else `OFF` | Build `examples/`. |
| `BLOC_WERROR` | `ON` for a top-level build, else `OFF` | Add `-Werror` to the library. |

**Warning: never set the configuration header on your own target only.** The header changes the
layout of `struct bloc_handle` and `struct bloc_pool`. If the library is built without it while
your code sees it, both sides disagree on the layout and the program breaks silently. Always use
`BLOC_CONFIG_HEADER` and `BLOC_CONFIG_DIRS` as shown above: they apply the header to the `bloc`
target as a PUBLIC definition, so the library and every consumer see the same configuration.

## Configuration

All options have defaults in [`src/include/bloc_opt.h`](src/include/bloc_opt.h) and are overridden in
your own header. Section 6 of [`docs/BLOC_SPEC.md`](docs/BLOC_SPEC.md#6-compile-time-configuration)
describes them in full, including the compile-time validation messages.

| Option | Default | Meaning |
| --- | --- | --- |
| `BLOC_BLOCK_ALIGNMENT` | `4` | Minimum block start alignment, power of two |
| `BLOC_PAYLOAD_ALIGNMENT` | `4` | Alignment of the data area start, power of two, `1` = off |
| `BLOC_SIZE_T` | `uint16_t` | Element size, offset and length type |
| `BLOC_COUNT_T` | `uint8_t` | Element count, active count and statistics type |
| `BLOC_REFCOUNT_T` | `uint8_t` | Reference count type |
| `BLOC_THREAD_SAFE` | `0` | `1` = protect pool and reference count updates with `BLOC_DECL_PROTECT`, `BLOC_PROTECT` and `BLOC_UNPROTECT` |
| `BLOC_CHECKS` | `1` | Parameter and bounds checks that return status codes |
| `BLOC_DEBUG` | `0` | Assertions for programming errors; requires `BLOC_CHECKS` |
| `BLOC_PLATFORM_ASSERT(msg)` | trap | Called when a debug assertion fails |
| `BLOC_ASSERT_MESSAGES` | `1` | `0` = the assertion handler gets a null pointer instead of a message, so no message text is compiled (saves about 2.6 KiB of RAM on classic AVRs) |
| `BLOC_STATS` | `0` | High-water mark and allocation-failure counter |

## Building and testing

The tests use Unity, fetched by CMake. Set `BLOC_UNITY_SOURCE_DIR` to a local Unity checkout to
work without network access. Build directories live under `build/`.

```sh
cmake --preset gcc-default                 # configurations: default debug nochecks wide noalign bigalign pthread
cmake --build --preset gcc-default
ctest --preset gcc-default                 # presets also exist for clang-<cfg>, <compiler>-<cfg>-rel and -size
```

| Script | Purpose |
| --- | --- |
| `scripts/run_all.sh [tier...]` | everything CI does, locally (tiers: gate host m32 emulated baremetal size coverage sanitize lto noheap fetchcontent) |
| `scripts/ci/build_one.sh` | build and test one target, configuration and build type |
| `scripts/coverage.sh [config...]` | GCC coverage; requires 100 % line, branch and function coverage of `src/bloc.c` in each of the six gated configurations (HTML reports in `build/coverage/`) |
| `scripts/sanitize.sh [--cc gcc\|clang] [--tsan] [config...]` | AddressSanitizer and UBSan over the gated configurations; `--tsan` runs the `pthread` configuration under ThreadSanitizer |
| `scripts/cross_check.sh` | cross-compile matrix of the bare-metal targets with symbol checks |
| `scripts/check_size.sh` | footprint report, budgets and regression baseline |
| `scripts/check_no_heap.sh` | no-heap symbol and source check |
| `scripts/fetchcontent_smoke.sh` | FetchContent consumer smoke tests |

## License

BLOC is released under the [MIT License](LICENSE).
