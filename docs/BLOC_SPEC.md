# BLOC — Buffer Lifetime & Ownership Control

Specification V1, revision 4 · 2026-10-03 · Andreas Steinbart

This document is normative. Words such as *must*, *must not* and *may* are requirements on the implementation. Revision history is in Appendix C.

## 1. Overview

BLOC is a small, deterministic, heap-free C11 library for fixed-size, reference-counted buffers with headroom, alignment and zero-copy header manipulation. It targets embedded systems where predictable memory use and low overhead matter.

Typical uses: network packets, protocol messages, DMA buffers, serial data, application messages and temporary binary data.

BLOC follows the proven parts of lwIP `pbuf` (`PBUF_POOL` allocation, headroom, `add_header`/`remove_header`, `ref`/`free`) but deliberately omits chaining and buffer types. Appendix A maps the APIs.

### Core properties

- No heap allocation inside BLOC; all storage is supplied by the caller.
- Fixed-size physical blocks, O(1) allocation and release.
- Intrusive free list stored inside free blocks; it costs no extra memory.
- Reference counting with overflow protection.
- Per-buffer `offset` and `len`; headroom is the offset.
- Zero-copy header add/remove and length adjustment.
- Compile-time block and payload alignment.
- Compile-time integer types for control structures.
- No relocation, compaction or chaining in V1.
- No OS dependency; optional locking via application-supplied macros.
- Minimal code size, with `.text` budgets for ARMv6-M and ARMv7-M (section 17).

## 2. Memory ownership, dependencies and pool model

BLOC never allocates or releases memory. The caller owns two objects: a `bloc_pool_t` control structure and one raw storage region holding all physical blocks.

```c
static bloc_pool_t pool;
static BLOC_POOL_STORAGE(storage, 8, 128);   /* correctly aligned, see section 4 */

bloc_pool_init(&pool, storage, sizeof(storage), 8, 128);
```

The storage may come from a static array, linker-defined memory or another allocator. BLOC only requires the size and alignment rules of sections 3 and 4.

Storage regions of different pools must not overlap, and a region must not be used for anything else while its pool is initialized. Section 11 relies on this: two distinct blocks never overlap.

A pool consists of `element_count` equally sized physical blocks at a fixed stride. Each block holds either an allocated buffer handle or a free-list node in the same header bytes, followed by a fixed data area of `element_size` bytes.

### No-heap and library dependency rules

These rules are hard requirements and are verified automatically (implementation plan, NH tests).

- BLOC must not call `malloc`, `calloc`, `realloc`, `free`, `aligned_alloc`, `memmove`, or any function that may allocate internally.
- The only C library functions BLOC may reference are `memcpy` and `memset`.
- BLOC sources and public headers include only `<stddef.h>`, `<stdint.h>`, `<stdbool.h>` and `<string.h>`. They must not include `<stdlib.h>`, `<stdio.h>` or `<assert.h>`. Some C libraries (e.g. newlib) allocate stdio buffers on first use, so even debug output must not go through stdio.
- The default assertion handler (section 6) traps without calling the C library.
- No variable-length arrays, no recursion, no floating point, no function-local `static` state.
- With `BLOC_DEBUG = 0`, compiled BLOC code must not call compiler runtime helpers (libgcc, compiler-rt), on any target. Concretely: no integer division or modulo, no 64-bit arithmetic on targets narrower than 64 bits, no software floating point. ARMv6-M, AVR and RV32I have no hardware divider, so a single `/` or `%` would pull in a runtime helper such as `__aeabi_uidivmod`.
- With `BLOC_DEBUG = 1`, the only runtime helpers allowed are unsigned integer division and modulo, needed by handle-validity step 5 (section 13).

## 3. Physical block layout and alignment

One alignment model applies: every block start is aligned to `BLOC_STORAGE_ALIGNMENT`, the header is padded to a multiple of `BLOC_PAYLOAD_ALIGNMENT`, and so every data area start is payload-aligned. The offset is never used to fix alignment.

```text
block start (aligned to BLOC_STORAGE_ALIGNMENT)
│
▼
┌──────────────────────────────────────────────┐
│ struct bloc_handle  (or free-list node)      │
├──────────────────────────────────────────────┤
│ padding to BLOC_PAYLOAD_ALIGNMENT            │
├──────────────────────────────────────────────┤ ◄ data_start (payload-aligned)
│ data area, element_size bytes                │
│ [ headroom ][ payload ][ tailroom ]          │
├──────────────────────────────────────────────┤
│ stride padding to BLOC_STORAGE_ALIGNMENT     │
└──────────────────────────────────────────────┘
```

### Alignment parameters

- `BLOC_BLOCK_ALIGNMENT` (BA): minimum alignment of every block start, e.g. for DMA or cache-line rules.
- `BLOC_PAYLOAD_ALIGNMENT` (PA): alignment of `data_start`. `1` disables payload alignment.
- Both must be powers of two (checked with `_Static_assert`).
- They may differ. The effective block alignment is derived, so `BA < PA` is legal and still correct.

```c
#define BLOC_STORAGE_ALIGNMENT \
    BLOC_MAX3(BLOC_BLOCK_ALIGNMENT, BLOC_PAYLOAD_ALIGNMENT, _Alignof(struct bloc_handle))
```

`_Alignof(struct bloc_handle)` is included because the header contains a pointer. A misaligned header would fault on cores without unaligned access, e.g. Cortex-M0. All three operands are powers of two, so `BLOC_STORAGE_ALIGNMENT` is one too.

### Payload alignment guarantee

The payload is aligned to PA directly after `bloc_alloc()`/`bloc_calloc()`, because the allocation offset is rounded up to PA (section 8). Operations that move the offset by arbitrary amounts (`bloc_add_header`, `bloc_remove_header`, `bloc_prepend*`) may leave the payload unaligned. This matches lwIP. Callers that need an aligned payload after header changes should use header sizes that are multiples of PA.

### Free blocks

A free block stores the free-list `next` pointer in the same union member as the owning-pool pointer (section 5). The `refcount` field is not overlaid and stays `0` while the block is free. No extra memory is used for the free list; the only list state outside the blocks is `free_head` in the caller-owned pool structure.

## 4. Pool size calculation and static storage

`BLOC_POOL_SIZE(n, e)` is an integer constant expression that includes every byte of per-block overhead. Callers never reproduce the layout calculation.

With H = `sizeof(struct bloc_handle)`, E = element size, PA = `BLOC_PAYLOAD_ALIGNMENT`, SA = `BLOC_STORAGE_ALIGNMENT`, N = element count:

```text
header_size  = align_up(H, PA)
block_stride = align_up(header_size + E, SA)
pool_size    = N * block_stride
```

```c
#define BLOC_ALIGN_UP(x, a)        (((size_t)(x) + ((size_t)(a) - 1u)) & ~((size_t)(a) - 1u))
#define BLOC_HEADER_SIZE           BLOC_ALIGN_UP(sizeof(struct bloc_handle), BLOC_PAYLOAD_ALIGNMENT)
#define BLOC_BLOCK_STRIDE(e)       BLOC_ALIGN_UP(BLOC_HEADER_SIZE + (size_t)(e), BLOC_STORAGE_ALIGNMENT)
#define BLOC_POOL_SIZE(n, e)       ((size_t)(n) * BLOC_BLOCK_STRIDE(e))

#define BLOC_POOL_STORAGE(name, n, e) \
    _Alignas(BLOC_STORAGE_ALIGNMENT) uint8_t name[BLOC_POOL_SIZE(n, e)]
```

The largest valid element size is the one whose stride still fits `BLOC_SIZE_T`:

```c
#define BLOC_ELEMENT_SIZE_MAX \
    (((size_t)BLOC_SIZE_MAX & ~((size_t)BLOC_STORAGE_ALIGNMENT - 1u)) - BLOC_HEADER_SIZE)
```

Because the macros need `sizeof` and `_Alignof`, `struct bloc_handle` and `struct bloc_pool` are complete types in the public header. Their fields are private by convention; applications use only the API. lwIP does the same with `struct pbuf`.

The storage base must be aligned to `BLOC_STORAGE_ALIGNMENT`. A plain `uint8_t` array has alignment 1, so callers should use `BLOC_POOL_STORAGE` or an equivalent `_Alignas`. `bloc_pool_init()` returns `BLOC_ALIGNMENT` for a misaligned base.

Placing handles in a byte array follows the lwIP `memp` idiom. The implementation accesses headers only through `struct bloc_handle` lvalues and payload only as bytes.

## 5. Buffer handle and data model

A buffer handle is embedded in its block and stores only `link`, `offset`, `len` and `refcount`. Everything else (element size, stride, alignment, capacity) is a pool property.

```c
struct bloc_handle {
    union {
        struct bloc_pool   *pool;      /* allocated: owning pool        */
        struct bloc_handle *next_free; /* free: next free block or NULL */
    } link;
    bloc_size_t     offset;            /* headroom = start of payload   */
    bloc_size_t     len;               /* payload length in bytes       */
    bloc_refcount_t refcount;          /* 0 = free                      */
};

typedef struct bloc_pool           bloc_pool_t;
typedef bloc_pool_t               *bloc_pool_handle_t;
typedef struct bloc_handle        *bloc_handle_t;
typedef const struct bloc_handle  *bloc_const_handle_t;
```

The field order is normative: largest first for minimal padding, and the union guarantees the free-list pointer never overlays `refcount`. On a 32-bit MCU with default types the header is 12 bytes (4 + 2 + 2 + 1, padded); on a 64-bit host it is 16 bytes.

`bloc_const_handle_t` exists because `const bloc_handle_t` means `struct bloc_handle * const` (constant pointer to mutable data). All read-only parameters use `bloc_const_handle_t`.

### Data model

```text
data_start                                         data_start + element_size
│                                                                   │
▼                                                                   ▼
┌──────────────────────┬────────────────────────┬─────────────────────┐
│ headroom = offset    │ payload = len          │ tailroom            │
└──────────────────────┴────────────────────────┴─────────────────────┘
                       ▲
                       bloc_data() = data_start + offset
```

```text
data_start = (uint8_t *)handle + BLOC_HEADER_SIZE
headroom   = offset
tailroom   = element_size - offset - len
```

The fundamental invariant is `offset + len <= element_size`. Only `offset`, `len` and `refcount` change during a buffer's lifetime; its physical block never moves.

## 6. Compile-time configuration

All options have defaults in `bloc_opt.h`, guarded by `#ifndef`. A project overrides them in its own header, named via `-DBLOC_CONFIG_HEADER="bloc_opts.h"` (analogous to lwIP `lwipopts.h`). `bloc.h` includes the project header first, then `bloc_opt.h`. Disabled features add no runtime storage and no code.

| Option | Default | Meaning |
| --- | --- | --- |
| `BLOC_BLOCK_ALIGNMENT` | `4` | Minimum block start alignment, power of two |
| `BLOC_PAYLOAD_ALIGNMENT` | `4` | `data_start` alignment, power of two, `1` = off |
| `BLOC_SIZE_T` | `uint16_t` | Element size, offset, len, header lengths |
| `BLOC_COUNT_T` | `uint8_t` | Element count, active count, statistics |
| `BLOC_REFCOUNT_T` | `uint8_t` | Reference count |
| `BLOC_THREAD_SAFE` | `0` | `1` = wrap pool and refcount updates in `BLOC_PROTECT` (section 12) |
| `BLOC_CHECKS` | `1` | Parameter and bounds checks that return status codes |
| `BLOC_DEBUG` | `0` | Assertions for programming errors (section 13); requires `BLOC_CHECKS = 1` |
| `BLOC_PLATFORM_ASSERT(msg)` | trap, see below | Called when a debug assertion fails; used only when `BLOC_DEBUG = 1` |
| `BLOC_STATS` | `0` | High-water mark and allocation-failure counter |

### Derived constants (public)

```c
#define BLOC_SIZE_MAX      ((BLOC_SIZE_T)-1)
#define BLOC_COUNT_MAX     ((BLOC_COUNT_T)-1)
#define BLOC_REFCOUNT_MAX  ((BLOC_REFCOUNT_T)-1)
```

### Assertion handler

As in lwIP (`LWIP_PLATFORM_ASSERT`), the configurable hook receives a message and is called only when an assertion has already failed. BLOC evaluates the condition itself. The default does not touch the C library:

```c
#ifndef BLOC_PLATFORM_ASSERT
#  if defined(__GNUC__) || defined(__clang__)
#    define BLOC_PLATFORM_ASSERT(msg)  do { (void)(msg); __builtin_trap(); } while (0)
#  else
#    define BLOC_PLATFORM_ASSERT(msg)  do { (void)(msg); for (;;) { } } while (0)
#  endif
#endif
```

An application may route it to its own fault handler, logger or breakpoint. If the hook returns, BLOC continues as described in section 13.

### Compile-time validation

Each rule is enforced with `_Static_assert` or `#error` using exactly the message given, so build tests can match it.

| Rule | Message |
| --- | --- |
| BA is a power of two ≥ 1 | `BLOC_BLOCK_ALIGNMENT must be a power of two` |
| PA is a power of two ≥ 1 | `BLOC_PAYLOAD_ALIGNMENT must be a power of two` |
| `(BLOC_SIZE_T)-1 > 0` | `BLOC_SIZE_T must be unsigned` |
| `(BLOC_COUNT_T)-1 > 0` | `BLOC_COUNT_T must be unsigned` |
| `(BLOC_REFCOUNT_T)-1 > 0` | `BLOC_REFCOUNT_T must be unsigned` |
| `sizeof(BLOC_SIZE_T) <= sizeof(size_t)` | `BLOC_SIZE_T must not be wider than size_t` |
| `BLOC_THREAD_SAFE == 1` requires `BLOC_PROTECT` | `BLOC_THREAD_SAFE requires BLOC_DECL_PROTECT, BLOC_PROTECT and BLOC_UNPROTECT` |
| `BLOC_DEBUG == 1` requires `BLOC_CHECKS == 1` | `BLOC_DEBUG requires BLOC_CHECKS` |

### Type usage

Per-buffer fields use only the configured small types. `size_t` is used only where a value spans the whole pool or where intermediate arithmetic must not overflow: `storage_size`, the layout macros and internal bounds arithmetic. A pool of 48 × 1536-byte Ethernet frames exceeds 64 KiB, so a 16-bit pool size would be a real limit.

## 7. Pool lifecycle

Initialization builds the full free list eagerly in O(N), like lwIP `memp_init`. Deinitialization is O(1) and succeeds only with no active buffers.

### Pool state

```c
struct bloc_pool {
    uint8_t            *storage;        /* NULL = not initialized          */
    struct bloc_handle *free_head;
    bloc_size_t         element_size;
    bloc_size_t         block_stride;
    bloc_count_t        element_count;  /* 0 = not initialized             */
    bloc_count_t        active_count;   /* allocated blocks, not refs      */
#if BLOC_STATS
    bloc_count_t        high_water;     /* max active_count seen           */
    bloc_count_t        alloc_failures; /* saturating                      */
#endif
};
```

`storage_size` is not stored: only `element_count * block_stride` bytes are used, and handle validation needs only that range. A zero-initialized `bloc_pool_t` (e.g. a static object) is a valid "not initialized" pool.

### bloc_pool_init

```c
bloc_status_t bloc_pool_init(bloc_pool_t *pool, void *storage, size_t storage_size,
                             bloc_count_t element_count, bloc_size_t element_size);
```

Checks run in this order; the first failing check determines the result.

1. `pool` or `storage` is `NULL`, `element_count == 0` or `element_size == 0` → `BLOC_INVALID`.
2. `element_size > BLOC_ELEMENT_SIZE_MAX` → `BLOC_INVALID`.
3. `storage` not aligned to `BLOC_STORAGE_ALIGNMENT` → `BLOC_ALIGNMENT`.
4. `storage_size < element_count * block_stride` (as an exact mathematical comparison) → `BLOC_BOUNDS`. The check must neither overflow nor divide (section 2); for example, subtract `block_stride` from the remaining size once per element. Init is O(N) anyway.

On success:

5. Link all blocks into the free list in ascending address order (block 0 is the head) and set each `refcount = 0`.
6. Set `active_count = 0` and reset statistics.

On failure the pool object and the storage are not modified. These checks are always compiled, independent of `BLOC_CHECKS`, because init is not on a hot path. Surplus storage beyond `element_count * block_stride` is ignored and never written. Initializing a pool that still has active buffers is a programming error.

### bloc_pool_deinit

```c
bloc_status_t bloc_pool_deinit(bloc_pool_t *pool);
```

- `NULL` or not initialized → `BLOC_INVALID`.
- `active_count > 0` → `BLOC_BUSY`, pool unchanged.
- Otherwise the pool is marked uninitialized (`element_count = 0`, `storage = NULL`, `free_head = NULL`) and may be initialized again. The caller's storage is not touched.

Using a handle after its pool has been deinitialized is undefined; `BLOC_DEBUG` detects it through the uninitialized marker.

### Statistics

```c
bloc_count_t  bloc_pool_free_count(const bloc_pool_t *pool);   /* always available */

#if BLOC_STATS
typedef struct {
    bloc_count_t high_water;
    bloc_count_t alloc_failures;
} bloc_pool_stats_t;

bloc_status_t bloc_pool_get_stats(const bloc_pool_t *pool, bloc_pool_stats_t *out);
#endif
```

`bloc_pool_free_count()` is derived from `element_count - active_count` and costs no storage. It returns `0` for a `NULL` (with `BLOC_CHECKS`) or uninitialized pool. `bloc_pool_get_stats()` returns `BLOC_INVALID` for a `NULL` pool, a `NULL` `out` or an uninitialized pool. `alloc_failures` counts only allocations that failed because the pool was empty and saturates at `BLOC_COUNT_MAX`.

## 8. Allocation

Headroom is requested per allocation, like the `layer` argument of lwIP `pbuf_alloc()`. The initial offset is the requested headroom rounded up to `BLOC_PAYLOAD_ALIGNMENT`, so the payload starts aligned.

```c
bloc_handle_t bloc_alloc (bloc_pool_handle_t pool, bloc_size_t headroom);
bloc_handle_t bloc_calloc(bloc_pool_handle_t pool, bloc_size_t headroom);
```

### bloc_alloc

1. `pool` is `NULL` (with `BLOC_CHECKS`) or not initialized → `NULL`.
2. Compute `offset = align_up(headroom, BLOC_PAYLOAD_ALIGNMENT)` in `size_t`. If `offset > element_size` → `NULL`. No block is taken and no failure is counted.
3. Take the first block from the free list. If the list is empty → `NULL`, and `alloc_failures` is incremented when `BLOC_STATS = 1`.
4. Set `link.pool = pool`, `refcount = 1`, `len = 0`, `offset` as computed.
5. Increment `active_count` and update `high_water`.

The data contents are undefined. Steps 3–5 run inside `BLOC_PROTECT` when `BLOC_THREAD_SAFE = 1`; steps 1 and 2 run before the lock. Allocation is O(1).

With `BLOC_DEBUG = 1`, step 1 asserts so that an invalid pool cannot be confused with an empty one during development. An empty pool and an oversize headroom are valid runtime conditions and do not assert.

The effective headroom may exceed the request by up to `PA − 1` bytes. Callers that need exact headroom pass a multiple of PA. `bloc_headroom()` always reports the effective value.

### bloc_calloc

Same logical state and failure behaviour as `bloc_alloc()`, plus the whole data area (`element_size` bytes from `data_start`, including headroom) is zeroed. Zeroing runs outside the lock. Cost is O(element_size). `bloc_calloc()` does not create a non-empty message: `len` stays `0`. On failure nothing is written.

## 9. Reference counting and shared buffers

A buffer starts with `refcount = 1`; `bloc_retain()` adds a reference and `bloc_release()` drops one, returning the block to its pool at zero. All references share one view (`offset`, `len`, data), so only a sole owner may modify the buffer.

```c
bloc_status_t bloc_retain (bloc_handle_t b);
bloc_status_t bloc_release(bloc_handle_t b);
```

### bloc_retain

| Condition | Result | State |
| --- | --- | --- |
| `b == NULL` | `BLOC_INVALID` (with `BLOC_CHECKS`) | unchanged |
| `refcount == 0` (block is free) | `BLOC_INVALID`, assert in debug | unchanged |
| `refcount == BLOC_REFCOUNT_MAX` | `BLOC_OVERFLOW` | unchanged, never wraps |
| otherwise | `BLOC_OK` | `refcount + 1` |

### bloc_release

| Condition | Result | State |
| --- | --- | --- |
| `b == NULL` | `BLOC_OK` | no-op, like `free(NULL)`; no lock taken; always compiled |
| `refcount == 0` (block is free) | `BLOC_INVALID`, assert in debug | unchanged |
| `refcount > 1` | `BLOC_OK` | `refcount − 1` |
| `refcount == 1` | `BLOC_OK` | `refcount = 0`, block pushed onto the head of the free list, `active_count − 1` |

Retain and release run inside `BLOC_PROTECT` when `BLOC_THREAD_SAFE = 1`. Both are O(1). The free-block and overflow checks are always compiled, independent of `BLOC_CHECKS`.

Because `link` is a union, `bloc_release()` must read `link.pool` before it writes `link.next_free`.

Releasing a free block is harmless only while it stays free. Once the block is reallocated, a stale handle refers to the new owner's buffer; using it is undefined and cannot be detected. Applications should set handles to `NULL` after the final release.

### Shared buffers

Retaining shares the buffer, not a copy: every holder sees the same `offset`, `len` and data. lwIP `pbuf` behaves the same way.

Rule: mutating operations require `refcount == 1`. With `BLOC_DEBUG = 1` they assert on `refcount > 1` and then perform the operation anyway; release builds do not check. Mutating operations are `bloc_set_len`, `bloc_add_header`, `bloc_remove_header`, `bloc_copy_from`, `bloc_copy` (destination), `bloc_append*` (destination) and `bloc_prepend*` (destination).

Read-only operations (`bloc_data`, `bloc_len`, `bloc_headroom`, `bloc_tailroom`, `bloc_copy_to`, use as a copy source) are allowed at any refcount.

Typical pattern: the producer fills a buffer, a queue retains it, the producer releases its reference, and from then on only the queue's consumer reads it.

## 10. Accessors and zero-copy length operations

Accessors read `offset` and `len`; the length operations change only `offset` and `len` and never move data. Together they make DMA, in-place serialization and header parsing possible without copies.

### Accessors

```c
void       *bloc_data    (bloc_const_handle_t b);  /* data_start + offset             */
bloc_size_t bloc_len     (bloc_const_handle_t b);  /* len                             */
bloc_size_t bloc_headroom(bloc_const_handle_t b);  /* offset                          */
bloc_size_t bloc_tailroom(bloc_const_handle_t b);  /* element_size - offset - len     */
```

`bloc_data()` takes a const handle and returns a mutable pointer, like `strchr()`, so it works for readers and writers alike. With `BLOC_CHECKS = 1` a `NULL` handle yields `NULL` or `0`. Accessors never take the lock.

### Length operations

```c
bloc_status_t bloc_set_len      (bloc_handle_t b, bloc_size_t len);
bloc_status_t bloc_add_header   (bloc_handle_t b, bloc_size_t n);
bloc_status_t bloc_remove_header(bloc_handle_t b, bloc_size_t n);
```

| Function | Precondition (else `BLOC_BOUNDS`) | Effect | lwIP equivalent |
| --- | --- | --- | --- |
| `bloc_set_len` | `len <= element_size - offset` | `len = len` | `pbuf_realloc` (shrink) |
| `bloc_add_header` | `n <= offset` | `offset -= n`, `len += n` | `pbuf_add_header` |
| `bloc_remove_header` | `n <= len` | `offset += n`, `len -= n` | `pbuf_remove_header` |

On failure the buffer is unchanged. `bloc_set_len` both grows and shrinks. Growing exposes bytes whose contents are whatever is in the block, which is the intended use after DMA or after writing through `bloc_data()`. Shrinking replaces a separate `trim_tail` function.

`bloc_add_header` exposes headroom without copying; the caller then writes the header through `bloc_data()`. Its new bytes are uninitialized. `bloc_prepend_data` (section 11) is the copying convenience variant.

### Examples

```c
/* RX via DMA */
bloc_handle_t b = bloc_alloc(&rx_pool, 0);
size_t n = dma_receive(bloc_data(b), bloc_tailroom(b));
bloc_set_len(b, (bloc_size_t)n);

/* parse and strip a 14-byte Ethernet header */
parse_eth(bloc_data(b));
bloc_remove_header(b, 14);

/* TX: reserve 40 bytes for IP+TCP, write payload, then headers in place */
bloc_handle_t t = bloc_alloc(&tx_pool, 40);
bloc_append_data(t, payload, payload_len);
bloc_add_header(t, 20); write_tcp_header(bloc_data(t));
bloc_add_header(t, 20); write_ip_header(bloc_data(t));
```

## 11. Copy, append and prepend

All copy operations use `memcpy` semantics and never move existing data. Because distinct blocks never overlap (section 2), the only possible overlap between two handles is a buffer with itself, and every such case below is either disjoint by construction or defined as a no-op.

```c
bloc_status_t bloc_copy_from   (bloc_handle_t dst, const void *src, bloc_size_t n);
bloc_status_t bloc_copy_to     (bloc_const_handle_t src, void *dst, bloc_size_t n, bloc_size_t pos);
bloc_status_t bloc_copy        (bloc_handle_t dst, bloc_const_handle_t src);

bloc_status_t bloc_append      (bloc_handle_t dst, bloc_const_handle_t src, bloc_size_t n);
bloc_status_t bloc_append_data (bloc_handle_t dst, const void *src, bloc_size_t n);
bloc_status_t bloc_prepend     (bloc_handle_t dst, bloc_const_handle_t src, bloc_size_t n);
bloc_status_t bloc_prepend_data(bloc_handle_t dst, const void *src, bloc_size_t n);
```

| Function | Reads | Writes at | Precondition (else `BLOC_BOUNDS`) | Result |
| --- | --- | --- | --- | --- |
| `bloc_copy_from` | `src[0..n)` | `dst.offset` | `n <= element_size − dst.offset` | `dst.len = n` |
| `bloc_copy_to` | payload `[pos, pos+n)` | `dst` | `pos <= len` and `n <= len − pos` | buffer unchanged |
| `bloc_copy` | whole `src` payload | `dst.offset` | `src.len <= element_size − dst.offset` | `dst.len = src.len` |
| `bloc_append*` | first `n` bytes of source | `dst.offset + dst.len` | BLOC source: `n <= src.len`; `n <= tailroom(dst)` | `dst.len += n` |
| `bloc_prepend*` | first `n` bytes of source | `dst.offset − n` | BLOC source: `n <= src.len`; `n <= headroom(dst)` | `dst.offset -= n`, `dst.len += n` |

The destination offset is always preserved by `copy_from` and `copy`; the source offset is never copied. Source and destination may belong to different pools. `n == 0` is valid and changes nothing except `len` in `copy_from` (set to `0`). On failure nothing is written and no field changes.

`bloc_copy_to` takes a start position `pos` within the payload, like lwIP `pbuf_copy_partial`. This lets parsers read a field without first stripping headers. Bounds are checked in the overflow-safe form shown.

### Self-copy rules

| Call | Source range | Destination range | Defined behaviour |
| --- | --- | --- | --- |
| `bloc_copy(b, b)` | identical | identical | No-op, returns `BLOC_OK` |
| `bloc_append(b, b, n)` | `[off, off+n)`, `n <= len` | `[off+len, off+len+n)` | Disjoint, allowed (duplicates the first `n` bytes at the end) |
| `bloc_prepend(b, b, n)` | `[off, off+n)` | `[off−n, off)` | Disjoint, allowed (duplicates the first `n` bytes at the front) |

For `bloc_copy_from`, `bloc_append_data` and `bloc_prepend_data`, the external pointer range `[src, src+n)` must not overlap the destination's write range. That is a caller error; `BLOC_DEBUG` checks it by comparing addresses converted to `uintptr_t`. No operation ever uses `memmove` or a temporary buffer.

## 12. Thread safety and ISR usage

With `BLOC_THREAD_SAFE = 1`, BLOC protects free-list and refcount updates through three application-defined macros modelled on lwIP `SYS_ARCH_PROTECT`. Buffer contents and `offset`/`len` are never locked; they belong to the buffer's owner.

```c
/* in bloc_opts.h, example for Cortex-M with interrupt masking */
#define BLOC_DECL_PROTECT(lev)   uint32_t lev
#define BLOC_PROTECT(lev)        do { (lev) = __get_PRIMASK(); __disable_irq(); } while (0)
#define BLOC_UNPROTECT(lev)      __set_PRIMASK(lev)
```

The macros are global, not per pool, so they add no pool fields. With `BLOC_THREAD_SAFE = 0` they expand to nothing and produce no unused-variable warnings.

### What is protected

| Operation | Protected section |
| --- | --- |
| `bloc_alloc`, `bloc_calloc` | Free-list pop, header init, `active_count`, statistics (parameter checks before, zeroing in `calloc` after) |
| `bloc_retain` | Free-block check, overflow check and increment |
| `bloc_release` | Free-block check, decrement, and on zero the free-list push and `active_count` |
| `bloc_pool_deinit` | `active_count` check and reset |
| `bloc_pool_free_count`, `bloc_pool_get_stats` | Consistent read |
| `bloc_pool_init` | Not protected: the pool must not be in use during init |
| Accessors, length, copy, append, prepend | Not protected: the caller owns the buffer (section 9) |

Every protected section is left on every path. `BLOC_PLATFORM_ASSERT` is never called while the lock is held: the condition is evaluated inside, the lock is released, then the assertion fires.

### ISR usage

BLOC needs no RTOS. A pool may be used from an ISR only if `BLOC_PROTECT` is safe there, which in practice means interrupt masking or an ISR-safe critical section. A mutex-based `BLOC_PROTECT` makes ISR use invalid. BLOC itself makes no assumption either way; the application documents its rule per pool.

## 13. Error handling and validation

Fallible operations return `bloc_status_t`; allocation returns `NULL`. Checks come in two tiers: `BLOC_CHECKS` returns status codes for bad parameters, `BLOC_DEBUG` additionally asserts on programming errors.

```c
typedef enum {
    BLOC_OK = 0,
    BLOC_INVALID,    /* NULL, uninitialized pool, free block, bad parameter */
    BLOC_BOUNDS,     /* length, offset or storage size out of range         */
    BLOC_BUSY,       /* pool still has active buffers                       */
    BLOC_OVERFLOW,   /* refcount at maximum                                 */
    BLOC_ALIGNMENT   /* storage base misaligned                             */
} bloc_status_t;
```

`BLOC_EMPTY` from the first draft is removed: allocation reports an empty pool with `NULL`, and no other operation needed it.

### Check tiers

| Check | `BLOC_CHECKS = 1` | `BLOC_DEBUG = 1` (adds) | `BLOC_CHECKS = 0` |
| --- | --- | --- | --- |
| `NULL` handle, pool or external pointer | `BLOC_INVALID` / `NULL` / `0` | assert | undefined |
| Length, offset, position bounds | `BLOC_BOUNDS` | assert | undefined |
| `pool_init` parameters, size and alignment | status | assert | still checked |
| Refcount overflow in `retain` | `BLOC_OVERFLOW` | – (valid runtime condition) | still checked |
| `retain`/`release` of a free block | `BLOC_INVALID` | assert | still checked |
| Handle validity (below) | – | assert, then bail | – |
| External pointer overlaps destination range | – | assert, then bail | – |
| Mutation with `refcount > 1` | – | assert, then continue | – |
| `active_count <= element_count` after alloc and release | – | assert, then continue | – |

"Bail" means: if `BLOC_PLATFORM_ASSERT` returns, the function returns `BLOC_INVALID` (or `NULL` / `0`) without any side effect. "Continue" means the operation completes normally. With the default trapping handler neither happens.

Empty pool, oversize headroom in `bloc_alloc` and refcount overflow are valid runtime conditions and never assert.

### Handle validity (`BLOC_DEBUG` only)

Every function that takes a handle, other than `bloc_retain`/`bloc_release` (which check the free block themselves), validates each non-`NULL` handle argument in this order:

1. `refcount != 0` (not a free block).
2. `link.pool != NULL`.
3. The pool is initialized (`storage != NULL`).
4. The handle address lies in `[storage, storage + element_count * block_stride)`.
5. `(handle − storage) % block_stride == 0`.

`bloc_retain`/`bloc_release` run steps 2–5 after their free-block check, inside the lock, and assert after unlocking.

### Check order

When several checks fail, the first in this order determines the result:

1. `NULL` handle or external pointer → `BLOC_INVALID`.
2. Handle validity (debug) → `BLOC_INVALID`.
3. Self-copy shortcut (`bloc_copy(b, b)` → `BLOC_OK`).
4. Bounds → `BLOC_BOUNDS`.
5. Overlap of external pointer (debug) → `BLOC_INVALID`.
6. Shared mutation (debug) → assert, continue.

No check allocates memory. Bounds checks are written so intermediate sums cannot overflow: for example `n > element_size - offset - len` instead of `offset + len + n > element_size`, with arithmetic in `size_t` where needed.

## 14. Invariants and complexity

The invariants below hold for every initialized pool at every point outside a protected section.

### Pool invariants

- `active_count <= element_count`
- `active_count + length(free list) == element_count`
- Every block start is aligned to `BLOC_STORAGE_ALIGNMENT`; every `data_start` to `BLOC_PAYLOAD_ALIGNMENT`.
- Free block: `refcount == 0`, linked exactly once in the free list.
- Allocated block: `refcount >= 1`, `link.pool` is its owning pool.

Only the first invariant is asserted at runtime (O(1)); the others are verified by the test suite.

### Buffer invariants

- `offset + len <= element_size`
- Directly after allocation: `offset == align_up(headroom, PA)`, `len == 0`, payload aligned to PA.
- After header operations the payload alignment is not guaranteed (section 3).

### Complexity

| Operation | Cost |
| --- | --- |
| `bloc_pool_init` | O(element_count) |
| `bloc_pool_deinit`, `bloc_pool_free_count`, `bloc_pool_get_stats` | O(1) |
| `bloc_alloc`, `bloc_retain`, `bloc_release` | O(1) |
| Accessors, `bloc_set_len`, `bloc_add_header`, `bloc_remove_header` | O(1) |
| `bloc_calloc` | O(element_size) |
| `bloc_copy_from`, `bloc_copy_to`, `bloc_copy`, `bloc_append*`, `bloc_prepend*` | O(bytes copied) |

No operation allocates memory, resizes a block or moves an existing payload.

## 15. V1 API summary

The complete V1 public API is 22 functions in seven groups (21 without `BLOC_STATS`). Public headers: `bloc.h` (API, types, layout macros) and `bloc_opt.h` (configuration defaults and validation).

```c
/* Pool lifecycle */
bloc_status_t bloc_pool_init      (bloc_pool_t *pool, void *storage, size_t storage_size,
                                   bloc_count_t element_count, bloc_size_t element_size);
bloc_status_t bloc_pool_deinit    (bloc_pool_t *pool);
bloc_count_t  bloc_pool_free_count(const bloc_pool_t *pool);
#if BLOC_STATS
bloc_status_t bloc_pool_get_stats (const bloc_pool_t *pool, bloc_pool_stats_t *out);
#endif

/* Allocation */
bloc_handle_t bloc_alloc          (bloc_pool_handle_t pool, bloc_size_t headroom);
bloc_handle_t bloc_calloc         (bloc_pool_handle_t pool, bloc_size_t headroom);

/* Lifetime */
bloc_status_t bloc_retain         (bloc_handle_t b);
bloc_status_t bloc_release        (bloc_handle_t b);

/* Access */
void         *bloc_data           (bloc_const_handle_t b);
bloc_size_t   bloc_len            (bloc_const_handle_t b);
bloc_size_t   bloc_headroom       (bloc_const_handle_t b);
bloc_size_t   bloc_tailroom       (bloc_const_handle_t b);

/* Zero-copy length operations */
bloc_status_t bloc_set_len        (bloc_handle_t b, bloc_size_t len);
bloc_status_t bloc_add_header     (bloc_handle_t b, bloc_size_t n);
bloc_status_t bloc_remove_header  (bloc_handle_t b, bloc_size_t n);

/* Copy */
bloc_status_t bloc_copy_from      (bloc_handle_t dst, const void *src, bloc_size_t n);
bloc_status_t bloc_copy_to        (bloc_const_handle_t src, void *dst, bloc_size_t n, bloc_size_t pos);
bloc_status_t bloc_copy           (bloc_handle_t dst, bloc_const_handle_t src);

/* Append / prepend */
bloc_status_t bloc_append         (bloc_handle_t dst, bloc_const_handle_t src, bloc_size_t n);
bloc_status_t bloc_append_data    (bloc_handle_t dst, const void *src, bloc_size_t n);
bloc_status_t bloc_prepend        (bloc_handle_t dst, bloc_const_handle_t src, bloc_size_t n);
bloc_status_t bloc_prepend_data   (bloc_handle_t dst, const void *src, bloc_size_t n);
```

### Changes against the original V1 draft

| Original | Revised | Reason |
| --- | --- | --- |
| `bloc_alloc(pool)` | `bloc_alloc(pool, headroom)` | Headroom was described but could not be requested |
| `bloc_size()` | `bloc_len()` | lwIP naming, avoids confusion with block size |
| `bloc_capacity()` | `bloc_headroom()`, `bloc_tailroom()` | Capacity mixed used and free bytes |
| – | `bloc_set_len`, `bloc_add_header`, `bloc_remove_header` | Zero-copy DMA, serialization and parsing |
| `bloc_copy_to(b, dst, n)` | `bloc_copy_to(b, dst, n, pos)` | Partial reads, like `pbuf_copy_partial` |
| `bloc_size_t storage_size` | `size_t storage_size` | Pools above 64 KiB |
| `const bloc_handle_t` | `bloc_const_handle_t` | Actual const-correctness |
| – | `bloc_pool_free_count`, `bloc_pool_get_stats` | Diagnostics |

## 16. Non-goals for V1

V1 deliberately excludes the following; each may be considered later but must not complicate the V1 core.

- Heap allocation and variable-size physical allocations
- Buffer chaining, `tot_len`, scatter/gather
- Copy-on-write, relocation, resizing, compaction
- Automatic headroom creation by moving data
- External-reference, ROM or network-specific buffer types (lwIP `PBUF_REF`, `PBUF_ROM`)
- OS-specific synchronization in the core
- Hidden metadata allocations
- Per-buffer capacity, alignment or headroom fields
- Pointer-based subviews into another buffer
- Moving buffers between pools

## 17. Code size

BLOC targets small microcontrollers, where flash is often the scarcest resource. Minimal `.text` footprint is a design requirement on a par with determinism, especially on ARMv6-M (Cortex-M0/M0+) and ARMv7-M (Cortex-M3/M4/M7).

### Measurement

Footprint is the full API: the sum of all `.text*` input sections of the object file built from the BLOC sources, with all public functions present. It is measured with:

- `arm-none-eabi-gcc` as the reference compiler (Clang is measured too, but has no budget)
- `-Os -mthumb -ffunction-sections -fdata-sections -std=c11`
- `-mcpu=cortex-m0plus` for ARMv6-M and `-mcpu=cortex-m3` for ARMv7-M
- no LTO

On ARM, literal pools are part of `.text` and so count against the budget.

### Budgets

The budgets are upper bounds for the reference compiler. An implementation that exceeds them is defective. The implementation plan adds a stricter regression baseline that may only shrink unless an increase is justified.

| Configuration | ARMv6-M (Cortex-M0+) | ARMv7-M (Cortex-M3) |
| --- | --- | --- |
| Defaults (`BLOC_CHECKS = 1`, `BLOC_DEBUG = 0`, `BLOC_STATS = 0`, `BLOC_THREAD_SAFE = 0`) | ≤ 1536 bytes | ≤ 1280 bytes |
| Defaults with `BLOC_CHECKS = 0` | ≤ 1152 bytes | ≤ 960 bytes |

Configurations with `BLOC_DEBUG`, `BLOC_STATS` or `BLOC_THREAD_SAFE` enabled, and other targets (ARMv7E-M, ARMv7-A/R Thumb, RV32, AVR), are measured and reported but have no budget.

### Section rules

- `.data` and `.bss` are `0` bytes in every configuration and on every target (BLOC has no global state, section 2).
- With `BLOC_DEBUG = 0`, `.rodata` is `0` bytes. With `BLOC_DEBUG = 1`, `.rodata` holds only assertion messages.

### Design rules

1. All executable code is in one translation unit. Public headers contain no function definitions, no `static inline` functions and no function-like macros that expand to statements.
2. Each public function is self-contained at link level. There are no function-pointer tables, no constructors and no cross-references that would keep unused API functions alive. Built with `-ffunction-sections` and linked with `--gc-sections`, an application pays only for the functions it calls plus their internal helpers.
3. No compiler runtime helpers (section 2). In particular, there is no division or modulo outside `BLOC_DEBUG` code.
4. Disabled features compile to nothing (section 6).
5. Shared logic, such as payload address computation or the common part of append/prepend/copy, is factored into internal `static` helpers wherever that reduces measured size. The compiler may inline them.
6. Per-buffer arithmetic uses the narrowest type that is correct. `size_t` is used only where section 6 requires it.
7. Footprint is a review criterion. A change that increases the measured size must be justified in its commit message.

## Appendix A: Mapping to lwIP pbuf

BLOC corresponds to a single, unchained `PBUF_POOL` pbuf with caller-owned pools.

| lwIP | BLOC | Difference |
| --- | --- | --- |
| `memp_init()` | `bloc_pool_init()` | Caller supplies pool and storage; several pools of any size |
| `pbuf_alloc(layer, len, PBUF_POOL)` | `bloc_alloc(pool, headroom)` | Headroom in bytes, not by layer; `len` starts at 0 |
| `pbuf_ref()` | `bloc_retain()` | Returns `BLOC_OVERFLOW` instead of asserting |
| `pbuf_free()` | `bloc_release()` | No chain walk; `NULL` is a no-op |
| `p->payload`, `p->len` | `bloc_data()`, `bloc_len()` | Accessors instead of fields |
| `pbuf_add_header()` | `bloc_add_header()` | Same semantics |
| `pbuf_remove_header()` | `bloc_remove_header()` | Same semantics |
| `pbuf_realloc()` (shrink only) | `bloc_set_len()` | Also grows into tailroom |
| `pbuf_take()` | `bloc_copy_from()` | Same semantics for one buffer |
| `pbuf_copy_partial()` | `bloc_copy_to()` | Same semantics for one buffer |
| `pbuf_copy()` | `bloc_copy()` | Destination offset preserved |
| `pbuf_cat()`, `pbuf_chain()` | – | No chaining in V1 |
| `SYS_ARCH_PROTECT()` | `BLOC_PROTECT()` | Same pattern |
| `LWIP_PLATFORM_ASSERT()` | `BLOC_PLATFORM_ASSERT()` | Same pattern, default traps without libc |
| `lwipopts.h` / `opt.h` | `BLOC_CONFIG_HEADER` / `bloc_opt.h` | Same pattern |

## Appendix B: Resolved review issues

All 26 findings of the first review are resolved; four were design decisions by the author, the rest follow lwIP practice or C11 rules.

| ID | Issue | Resolution | Section | Basis |
| --- | --- | --- | --- | --- |
| A1 | Headroom described but not requestable | `bloc_alloc(pool, headroom)` | 8 | Author decision |
| A2 | No zero-copy length control (DMA, parsing) | `bloc_set_len`, `bloc_add_header`, `bloc_remove_header` | 10 | lwIP |
| A3 | Two alignment mechanisms; wrong for `BA < PA` | Single model, `BLOC_STORAGE_ALIGNMENT` = max of BA, PA, handle alignment | 3 | Best practice |
| A4 | Alignment invariant contradicts prepend | Guaranteed only after allocation | 3, 14 | lwIP |
| A5 | Opaque handle vs. `sizeof` in pool-size macro | Complete types, private by convention | 4 | lwIP |
| A6 | 16-bit `storage_size` limits pools to 64 KiB | `size_t storage_size` | 6, 7 | Best practice |
| A7 | Init claimed O(1) but builds free list | Eager init, documented as O(N) | 7, 14 | Author decision |
| A8 | `const bloc_handle_t` is not pointer-to-const | `bloc_const_handle_t` | 5 | C11 |
| B1 | Double release only harmless while free; layout unclear | Union for link, `refcount` never overlaid, stale-handle use undefined | 5, 9 | Best practice |
| B2 | Shared references share the view | Mutation requires `refcount == 1`, debug assert | 9 | Author decision |
| B3 | Lock scope and hook form undefined | Global `BLOC_PROTECT` macros, scope table | 12 | lwIP |
| B4 | Overlap rules mostly moot | Concrete self-copy table, no `memmove` | 11 | Analysis |
| B5 | Source bytes for BLOC append/prepend unspecified | First `n` bytes of source payload | 11 | Best practice |
| B6 | `bloc_capacity` / `bloc_size` misleading | `bloc_len`, `bloc_headroom`, `bloc_tailroom` | 10 | Author decision |
| B7 | `NULL` behaviour undefined | Defined per function | 9, 10, 13 | Best practice |
| C1 | No switch for checks vs. debug | `BLOC_CHECKS`, `BLOC_DEBUG`, `BLOC_PLATFORM_ASSERT` | 6, 13 | lwIP |
| C2 | Example storage not aligned | `BLOC_POOL_STORAGE` with `_Alignas` | 4 | C11 |
| C3 | Strict aliasing on byte storage | Documented `memp` idiom and access rule | 4 | lwIP |
| C4 | Overflow in bounds arithmetic | Subtraction-form checks | 13 | Best practice |
| C5 | Handle field order unspecified | Normative order, largest first | 5 | Best practice |
| D1 | `BLOC_EMPTY` unused | Removed | 13 | Best practice |
| D2 | No release outcome beyond status | Kept simple: status only | 9 | Best practice |
| D3 | No pool statistics | `bloc_pool_free_count`, optional stats | 7 | lwIP `MEMP_STATS` |
| D4 | Deinit leaves pool looking valid | Uninitialized marker | 7 | Best practice |
| D5 | Missing init parameter checks | Explicit validation order | 7 | Best practice |
| D6 | `calloc` zeroing scope unclear | Whole data area incl. headroom, outside lock | 8 | Best practice |

## Appendix C: Revision history

| Revision | Date | Changes |
| --- | --- | --- |
| 1 | – | First draft (47 sections) |
| 2 | 2026-10-03 | Review resolved (Appendix B), API revised (section 15) |
| 3 | 2026-10-03 | No-heap and libc dependency rules (section 2); `BLOC_ASSERT(x)` replaced by lwIP-style `BLOC_PLATFORM_ASSERT(msg)` with a libc-free trapping default; `BLOC_DEBUG` requires `BLOC_CHECKS`; exact static-assert messages; `BLOC_ELEMENT_SIZE_MAX` and `*_MAX` constants; pool unchanged on failed init; bail/continue semantics after a returning assertion; handle-validity steps and check order; asserts never under lock; runtime invariant assert reduced to the O(1) one; header split into `bloc.h` and `bloc_opt.h` |
| 4 | 2026-10-03 | Code-size requirement with `.text` budgets for ARMv6-M and ARMv7-M (section 17); no compiler runtime helpers without `BLOC_DEBUG` (section 2); `bloc_pool_init` size check without division (section 7) |
