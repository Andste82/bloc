/**
 * @file bloc.h
 * @brief BLOC - Buffer Lifetime & Offset Control: public API, types and layout macros.
 *
 * BLOC manages a fixed number of equally sized buffers in caller-provided storage. It never
 * allocates memory. A buffer is a handle embedded in its block; the payload is a window
 * (offset, len) into the block's data area, so headers can be added or removed without copying.
 *
 * Project options live in a header named by BLOC_CONFIG_HEADER and are validated in bloc_opt.h.
 * The fields of struct bloc_handle and struct bloc_pool are private by convention: they are
 * public only because the layout macros need sizeof and _Alignof. Applications use the API.
 */
#ifndef BLOC_H
#define BLOC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef BLOC_CONFIG_HEADER
#include BLOC_CONFIG_HEADER
#endif

#include "bloc_opt.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @name Version */
/**@{*/
#define BLOC_VERSION_MAJOR 1
#define BLOC_VERSION_MINOR 0
#define BLOC_VERSION_PATCH 0
/**@}*/

/** Type of element sizes, offsets and lengths (BLOC_SIZE_T). */
typedef BLOC_SIZE_T bloc_size_t;
/** Type of element counts, the active count and statistics (BLOC_COUNT_T). */
typedef BLOC_COUNT_T bloc_count_t;
/** Type of the per-buffer reference count (BLOC_REFCOUNT_T). */
typedef BLOC_REFCOUNT_T bloc_refcount_t;

/** Largest value of bloc_size_t. */
#define BLOC_SIZE_MAX ((BLOC_SIZE_T) - 1)
/** Largest value of bloc_count_t. */
#define BLOC_COUNT_MAX ((BLOC_COUNT_T) - 1)
/** Largest value of bloc_refcount_t. */
#define BLOC_REFCOUNT_MAX ((BLOC_REFCOUNT_T) - 1)

/** Result of every fallible operation. Allocation reports failure with NULL instead. */
typedef enum {
    /** The operation succeeded. */
    BLOC_OK = 0,
    /** NULL argument, uninitialized pool, free block or bad parameter. */
    BLOC_INVALID,
    /** Length, offset or storage size out of range. */
    BLOC_BOUNDS,
    /** The pool still has active buffers. */
    BLOC_BUSY,
    /** The reference count is at its maximum. */
    BLOC_OVERFLOW,
    /** The storage base is misaligned. */
    BLOC_ALIGNMENT
} bloc_status_t;

struct bloc_pool;

/**
 * Buffer handle, embedded at the start of its block. The field order is normative: the union
 * keeps the free-list pointer from overlaying refcount, and largest-first minimizes padding.
 * Data model: headroom = offset, payload = len, tailroom = element_size - offset - len.
 */
struct bloc_handle {
    union {
        struct bloc_pool *pool;        /* allocated: owning pool        */
        struct bloc_handle *next_free; /* free: next free block or NULL */
    } link;
    bloc_size_t offset;       /* headroom = start of payload   */
    bloc_size_t len;          /* payload length in bytes       */
    bloc_refcount_t refcount; /* 0 = free                      */
};

/**
 * Pool state, owned by the caller. A zero-initialized pool (for example a static object) is a
 * valid "not initialized" pool.
 */
struct bloc_pool {
    uint8_t *storage; /* NULL = not initialized */
    struct bloc_handle *free_head;
    bloc_size_t element_size;
    bloc_size_t block_stride;
    bloc_count_t element_count; /* 0 = not initialized             */
    bloc_count_t active_count;  /* allocated blocks, not refs      */
#if BLOC_STATS
    bloc_count_t high_water;     /* max active_count seen           */
    bloc_count_t alloc_failures; /* saturating                      */
#endif
};

/** Pool object. */
typedef struct bloc_pool bloc_pool_t;
/** Pointer to a pool. */
typedef bloc_pool_t *bloc_pool_handle_t;
/** Mutable buffer handle. */
typedef struct bloc_handle *bloc_handle_t;
/** Read-only buffer handle; `const bloc_handle_t` would only make the pointer constant. */
typedef const struct bloc_handle *bloc_const_handle_t;

#if BLOC_STATS
/** Pool statistics, available when BLOC_STATS is 1. */
typedef struct {
    bloc_count_t high_water;
    bloc_count_t alloc_failures;
} bloc_pool_stats_t;
#endif

/* --- Layout macros (all integer constant expressions) ----------------------------------- */

/** Larger of two values. Evaluates its arguments more than once. */
#define BLOC_MAX2(a, b) ((a) > (b) ? (a) : (b))
/** Largest of three values, compared as size_t. */
#define BLOC_MAX3(a, b, c) BLOC_MAX2(BLOC_MAX2((size_t)(a), (size_t)(b)), (size_t)(c))

/**
 * Alignment of every block start: max(BLOC_BLOCK_ALIGNMENT, BLOC_PAYLOAD_ALIGNMENT,
 * _Alignof(struct bloc_handle)). A power of two. The storage base passed to bloc_pool_init()
 * must be aligned to it.
 */
#define BLOC_STORAGE_ALIGNMENT                                                                     \
    BLOC_MAX3(BLOC_BLOCK_ALIGNMENT, BLOC_PAYLOAD_ALIGNMENT, _Alignof(struct bloc_handle))

/** Rounds x up to a multiple of the power of two a, as size_t. */
#define BLOC_ALIGN_UP(x, a) (((size_t)(x) + ((size_t)(a) - 1u)) & ~((size_t)(a) - 1u))

/** Size of the block header: sizeof(struct bloc_handle) padded to BLOC_PAYLOAD_ALIGNMENT. */
#define BLOC_HEADER_SIZE BLOC_ALIGN_UP(sizeof(struct bloc_handle), BLOC_PAYLOAD_ALIGNMENT)

/** Distance between two blocks for element size e: header plus e, padded to the storage alignment.
 */
#define BLOC_BLOCK_STRIDE(e) BLOC_ALIGN_UP(BLOC_HEADER_SIZE + (size_t)(e), BLOC_STORAGE_ALIGNMENT)

/** Storage bytes needed for n elements of size e, including all per-block overhead. */
#define BLOC_POOL_SIZE(n, e) ((size_t)(n) * BLOC_BLOCK_STRIDE(e))

/** Declares a suitably aligned storage array `name` for n elements of size e. */
#define BLOC_POOL_STORAGE(name, n, e)                                                              \
    _Alignas(BLOC_STORAGE_ALIGNMENT) uint8_t name[BLOC_POOL_SIZE(n, e)]

/** Largest element size whose block stride still fits bloc_size_t. */
#define BLOC_ELEMENT_SIZE_MAX                                                                      \
    (((size_t)BLOC_SIZE_MAX & ~((size_t)BLOC_STORAGE_ALIGNMENT - 1u)) - BLOC_HEADER_SIZE)

/* --- Pool lifecycle --------------------------------------------------------------------- */

/**
 * Initializes a pool over caller-provided storage and links all blocks into the free list in
 * ascending address order. O(element_count). Not protected: the pool must not be in use.
 *
 * Checks run in this order and are always compiled; the first failure decides the result and
 * leaves the pool and the storage unmodified:
 * 1. pool or storage is NULL, element_count or element_size is 0: BLOC_INVALID.
 * 2. element_size > BLOC_ELEMENT_SIZE_MAX: BLOC_INVALID.
 * 3. storage is not aligned to BLOC_STORAGE_ALIGNMENT: BLOC_ALIGNMENT.
 * 4. storage_size < element_count * block stride: BLOC_BOUNDS.
 *
 * Storage beyond element_count * block stride is never written. With BLOC_DEBUG a failing
 * parameter check also raises an assertion.
 *
 * Thread safety: not protected; the pool must not be in use.
 *
 * @param pool          Pool object to initialize.
 * @param storage       Storage base, aligned to BLOC_STORAGE_ALIGNMENT.
 * @param storage_size  Size of the storage in bytes (at least BLOC_POOL_SIZE).
 * @param element_count Number of buffers.
 * @param element_size  Data area size of each buffer in bytes.
 * @return BLOC_OK, BLOC_INVALID, BLOC_ALIGNMENT or BLOC_BOUNDS.
 */
bloc_status_t bloc_pool_init(bloc_pool_t *pool, void *storage, size_t storage_size,
                             bloc_count_t element_count, bloc_size_t element_size);

/**
 * Marks an idle pool as uninitialized so it can be initialized again. O(1). The storage is not
 * touched.
 *
 * Thread safety: runs under BLOC_PROTECT when BLOC_THREAD_SAFE is 1.
 *
 * @param pool Pool to deinitialize.
 * @return BLOC_OK; BLOC_INVALID for NULL or an uninitialized pool; BLOC_BUSY while buffers are
 *         active (pool unchanged).
 */
bloc_status_t bloc_pool_deinit(bloc_pool_t *pool);

/**
 * Number of free blocks (element_count - active_count). O(1), always available.
 *
 * Thread safety: runs under BLOC_PROTECT when BLOC_THREAD_SAFE is 1.
 *
 * @param pool Pool to query.
 * @return Free block count; 0 for NULL (with BLOC_CHECKS) or an uninitialized pool.
 */
bloc_count_t bloc_pool_free_count(const bloc_pool_t *pool);

#if BLOC_STATS
/**
 * Reads the pool statistics: high-water mark of active buffers and the number of allocations
 * that failed because the pool was empty (saturating at BLOC_COUNT_MAX). Available when
 * BLOC_STATS is 1.
 *
 * Thread safety: runs under BLOC_PROTECT when BLOC_THREAD_SAFE is 1.
 *
 * @param pool Pool to query.
 * @param out  Receives the statistics.
 * @return BLOC_OK, or BLOC_INVALID for NULL pool, NULL out or an uninitialized pool.
 */
bloc_status_t bloc_pool_get_stats(const bloc_pool_t *pool, bloc_pool_stats_t *out);
#endif

/* --- Allocation and lifetime ------------------------------------------------------------ */

/**
 * Takes a block from the pool. The new buffer has refcount 1, len 0 and an offset of the
 * requested headroom rounded up to BLOC_PAYLOAD_ALIGNMENT, so the payload starts aligned and
 * bloc_headroom() may exceed the request by up to PA - 1. The data contents are undefined.
 * O(1).
 *
 * Thread safety: runs under BLOC_PROTECT when BLOC_THREAD_SAFE is 1.
 *
 * @param pool     Pool to allocate from.
 * @param headroom Requested headroom in bytes.
 * @return The buffer, or NULL for a NULL or uninitialized pool (asserts with BLOC_DEBUG), an
 *         oversize headroom, or an empty pool (counted in the statistics).
 */
bloc_handle_t bloc_alloc(bloc_pool_handle_t pool, bloc_size_t headroom);

/**
 * Like bloc_alloc(), and additionally zeroes the whole data area (including the headroom).
 * len stays 0. O(element_size). On failure nothing is written.
 *
 * Thread safety: the allocation runs under BLOC_PROTECT when BLOC_THREAD_SAFE is 1;
 * the zeroing runs after the lock is released.
 *
 * @param pool     Pool to allocate from.
 * @param headroom Requested headroom in bytes.
 * @return The buffer or NULL, as for bloc_alloc().
 */
bloc_handle_t bloc_calloc(bloc_pool_handle_t pool, bloc_size_t headroom);

/**
 * Adds a reference. All references share one view (offset, len, data). O(1).
 *
 * Thread safety: runs under BLOC_PROTECT when BLOC_THREAD_SAFE is 1.
 *
 * @param b Buffer to retain.
 * @return BLOC_OK; BLOC_INVALID for NULL (with BLOC_CHECKS) or a free block (asserts with
 *         BLOC_DEBUG); BLOC_OVERFLOW if the refcount is BLOC_REFCOUNT_MAX (it never wraps).
 */
bloc_status_t bloc_retain(bloc_handle_t b);

/**
 * Drops a reference; at zero the block returns to the head of its pool's free list. O(1).
 * Set handles to NULL after the final release: a stale handle cannot be detected once the block
 * is reallocated.
 *
 * Thread safety: runs under BLOC_PROTECT when BLOC_THREAD_SAFE is 1.
 *
 * @param b Buffer to release.
 * @return BLOC_OK (also for NULL, which is a no-op); BLOC_INVALID for a free block (asserts with
 *         BLOC_DEBUG).
 */
bloc_status_t bloc_release(bloc_handle_t b);

/* --- Access and zero-copy length operations --------------------------------------------- */

/**
 * Start of the payload (data_start + offset). Takes a const handle and returns a mutable
 * pointer, like strchr(), so it serves readers and writers. Never takes the lock.
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param b Buffer.
 * @return Payload pointer; NULL for a NULL handle (with BLOC_CHECKS).
 */
void *bloc_data(bloc_const_handle_t b);

/**
 * Payload length in bytes.
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param b Buffer.
 * @return len; 0 for a NULL handle (with BLOC_CHECKS).
 */
bloc_size_t bloc_len(bloc_const_handle_t b);

/**
 * Free bytes in front of the payload (the offset).
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param b Buffer.
 * @return offset; 0 for a NULL handle (with BLOC_CHECKS).
 */
bloc_size_t bloc_headroom(bloc_const_handle_t b);

/**
 * Free bytes behind the payload (element_size - offset - len).
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param b Buffer.
 * @return Tailroom; 0 for a NULL handle (with BLOC_CHECKS).
 */
bloc_size_t bloc_tailroom(bloc_const_handle_t b);

/**
 * Sets the payload length without moving data; grows or shrinks. Grown bytes keep whatever the
 * block contains. Requires len <= element_size - offset. Mutating: needs refcount 1.
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param b   Buffer.
 * @param len New length.
 * @return BLOC_OK; BLOC_BOUNDS if len does not fit (buffer unchanged); BLOC_INVALID for NULL.
 */
bloc_status_t bloc_set_len(bloc_handle_t b, bloc_size_t len);

/**
 * Exposes n bytes of headroom as payload (offset -= n, len += n) without copying. The new bytes
 * are uninitialized. Requires n <= offset. Mutating: needs refcount 1.
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param b Buffer.
 * @param n Number of bytes.
 * @return BLOC_OK; BLOC_BOUNDS if n > headroom (buffer unchanged); BLOC_INVALID for NULL.
 */
bloc_status_t bloc_add_header(bloc_handle_t b, bloc_size_t n);

/**
 * Strips n bytes from the front of the payload (offset += n, len -= n) without copying.
 * Requires n <= len. Mutating: needs refcount 1.
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param b Buffer.
 * @param n Number of bytes.
 * @return BLOC_OK; BLOC_BOUNDS if n > len (buffer unchanged); BLOC_INVALID for NULL.
 */
bloc_status_t bloc_remove_header(bloc_handle_t b, bloc_size_t n);

/* --- Copy, append and prepend ----------------------------------------------------------- */

/**
 * Copies n bytes from external memory to the destination offset and sets len = n. The source
 * range must not overlap the destination range. Requires n <= element_size - offset.
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param dst Destination buffer (refcount 1).
 * @param src External source, at least n bytes.
 * @param n   Number of bytes (0 is valid and sets len to 0).
 * @return BLOC_OK; BLOC_BOUNDS if n does not fit; BLOC_INVALID for NULL or (BLOC_DEBUG) overlap.
 *         On failure nothing is written.
 */
bloc_status_t bloc_copy_from(bloc_handle_t dst, const void *src, bloc_size_t n);

/**
 * Copies n payload bytes starting at position pos into external memory. The buffer is not
 * changed. Requires pos <= len and n <= len - pos. Allowed at any refcount.
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param src Source buffer.
 * @param dst External destination, at least n bytes.
 * @param n   Number of bytes.
 * @param pos Start position within the payload.
 * @return BLOC_OK; BLOC_BOUNDS if the range is outside the payload; BLOC_INVALID for NULL.
 */
bloc_status_t bloc_copy_to(bloc_const_handle_t src, void *dst, bloc_size_t n, bloc_size_t pos);

/**
 * Copies the whole payload of src to the destination offset and sets dst.len = src.len. The
 * destination offset is kept; the source offset is never copied. The pools may differ.
 * bloc_copy(b, b) is a no-op returning BLOC_OK. Requires src.len <= element_size - dst.offset.
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param dst Destination buffer (refcount 1).
 * @param src Source buffer.
 * @return BLOC_OK; BLOC_BOUNDS if the payload does not fit; BLOC_INVALID for NULL.
 */
bloc_status_t bloc_copy(bloc_handle_t dst, bloc_const_handle_t src);

/**
 * Appends the first n payload bytes of src behind the destination payload (len += n).
 * bloc_append(b, b, n) is allowed. Requires n <= src.len and n <= tailroom(dst).
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param dst Destination buffer (refcount 1).
 * @param src Source buffer.
 * @param n   Number of bytes.
 * @return BLOC_OK; BLOC_BOUNDS if either limit is exceeded; BLOC_INVALID for NULL.
 */
bloc_status_t bloc_append(bloc_handle_t dst, bloc_const_handle_t src, bloc_size_t n);

/**
 * Appends n bytes of external memory behind the destination payload (len += n). The source range
 * must not overlap the destination's write range. Requires n <= tailroom(dst).
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param dst Destination buffer (refcount 1).
 * @param src External source, at least n bytes.
 * @param n   Number of bytes.
 * @return BLOC_OK; BLOC_BOUNDS if n > tailroom; BLOC_INVALID for NULL or (BLOC_DEBUG) overlap.
 */
bloc_status_t bloc_append_data(bloc_handle_t dst, const void *src, bloc_size_t n);

/**
 * Prepends the first n payload bytes of src in front of the destination payload (offset -= n,
 * len += n). bloc_prepend(b, b, n) is allowed. Requires n <= src.len and n <= headroom(dst).
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param dst Destination buffer (refcount 1).
 * @param src Source buffer.
 * @param n   Number of bytes.
 * @return BLOC_OK; BLOC_BOUNDS if either limit is exceeded; BLOC_INVALID for NULL.
 */
bloc_status_t bloc_prepend(bloc_handle_t dst, bloc_const_handle_t src, bloc_size_t n);

/**
 * Prepends n bytes of external memory in front of the destination payload (offset -= n,
 * len += n). The source range must not overlap the destination's write range. Requires
 * n <= headroom(dst).
 *
 * Thread safety: not protected; the caller owns the buffer (spec section 9).
 *
 * @param dst Destination buffer (refcount 1).
 * @param src External source, at least n bytes.
 * @param n   Number of bytes.
 * @return BLOC_OK; BLOC_BOUNDS if n > headroom; BLOC_INVALID for NULL or (BLOC_DEBUG) overlap.
 */
bloc_status_t bloc_prepend_data(bloc_handle_t dst, const void *src, bloc_size_t n);

#ifdef __cplusplus
}
#endif

#endif /* BLOC_H */
