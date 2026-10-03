/*
 * BLOC implementation: pool lifecycle, allocation, accessors, reference counting and the
 * zero-copy length operations.
 *
 * All executable code of the library lives in this translation unit. Compile-time options come
 * from bloc_opt.h. See docs/BLOC_SPEC.md for the behaviour and docs/IMPLEMENTATION_PLAN.md,
 * section 4, for the structure of this file.
 */
#include <string.h>

#include "bloc.h"

#if BLOC_DEBUG
#define BLOC_I_FAIL(msg) BLOC_PLATFORM_ASSERT(msg)
#else
#define BLOC_I_FAIL(msg) ((void)0)
#endif

/* Always-compiled check with assert-in-debug and early return. */
#define BLOC_I_REQUIRE(cond, msg, ret)                                                             \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            BLOC_I_FAIL(msg);                                                                      \
            return (ret);                                                                          \
        }                                                                                          \
    } while (0)

/* Check compiled only with BLOC_CHECKS. */
#if BLOC_CHECKS
#define BLOC_I_CHECK(cond, msg, ret) BLOC_I_REQUIRE(cond, msg, ret)
#else
#define BLOC_I_CHECK(cond, msg, ret) ((void)0)
#endif

#if BLOC_THREAD_SAFE
#define BLOC_I_LOCK_DECL(l) BLOC_DECL_PROTECT(l)
#define BLOC_I_LOCK(l) BLOC_PROTECT(l)
#define BLOC_I_UNLOCK(l) BLOC_UNPROTECT(l)
#else
#define BLOC_I_LOCK_DECL(l) /* nothing */
#define BLOC_I_LOCK(l) ((void)0)
#define BLOC_I_UNLOCK(l) ((void)0)
#endif

/* --- Internal helpers --------------------------------------------------------------------- */

/* Start of the data area of a block: the header padded to BLOC_PAYLOAD_ALIGNMENT. */
static uint8_t *bloc_i_data_start(const struct bloc_handle *b)
{
    return (uint8_t *)(uintptr_t)b + BLOC_HEADER_SIZE;
}

#if BLOC_DEBUG
/*
 * Handle validity steps 2 to 5 (spec section 13) as an expression. The handle must have a
 * non-zero refcount, so that link.pool is meaningful (step 1 is done by BLOC_I_HANDLE_VALID or by
 * the caller). The range check is done on the byte distance to the storage base with unsigned
 * wrap-around, so a handle below the storage fails it too. Distance and stride are divided
 * instead of multiplying count and stride, because a multiplication would need a compiler
 * runtime helper on cores without a multiplier (RV32I), while the spec allows the unsigned
 * division and modulo helpers in debug builds.
 *
 * This is a macro and not a function on purpose: called out of line, the helper keeps the handle
 * alive across a call in every accessor, and GCC for PowerPC then emits libgcc register
 * save/restore helpers (_savegpr_*, _restgpr_*) at -Os, which R-03 does not allow. R-04 forbids
 * the inline attributes that would avoid this. The argument is evaluated several times and must
 * be a plain variable.
 */
#define BLOC_I_ADDR_VALID(b)                                                                       \
    ((b)->link.pool != NULL && (b)->link.pool->storage != NULL &&                                  \
     ((uintptr_t)(b) - (uintptr_t)(b)->link.pool->storage) /                                       \
             (uintptr_t)(b)->link.pool->block_stride <                                             \
         (uintptr_t)(b)->link.pool->element_count &&                                               \
     ((uintptr_t)(b) - (uintptr_t)(b)->link.pool->storage) %                                       \
             (uintptr_t)(b)->link.pool->block_stride ==                                            \
         0u)

/* Handle validity step 1 followed by steps 2 to 5. */
#define BLOC_I_HANDLE_VALID(b) ((b)->refcount != 0u && BLOC_I_ADDR_VALID(b))

/* Mutation of a shared buffer is a programming error: assert, then continue (spec section 13). */
#define BLOC_I_SHARED_MUTATION(b, msg)                                                             \
    do {                                                                                           \
        if ((b)->refcount > 1u) {                                                                  \
            BLOC_I_FAIL(msg);                                                                      \
        }                                                                                          \
    } while (0)
#else
#define BLOC_I_SHARED_MUTATION(b, msg) ((void)0)
#endif

/* --- Pool lifecycle ----------------------------------------------------------------------- */

bloc_status_t bloc_pool_init(bloc_pool_t *pool, void *storage, size_t storage_size,
                             bloc_count_t element_count, bloc_size_t element_size)
{
    struct bloc_handle *b;
    uint8_t *base;
    size_t stride;
    size_t rem;
    size_t span;
    size_t off;
    bloc_count_t i;

    BLOC_I_REQUIRE(pool != NULL && storage != NULL && element_count != 0u && element_size != 0u,
                   "bloc_pool_init: invalid argument", BLOC_INVALID);
    BLOC_I_REQUIRE((size_t)element_size <= BLOC_ELEMENT_SIZE_MAX,
                   "bloc_pool_init: element_size too large", BLOC_INVALID);
    BLOC_I_REQUIRE(((uintptr_t)storage & ((uintptr_t)BLOC_STORAGE_ALIGNMENT - 1u)) == 0u,
                   "bloc_pool_init: storage is misaligned", BLOC_ALIGNMENT);

    /* Exact size check without multiplication or division: subtract one stride per element. */
    stride = BLOC_BLOCK_STRIDE(element_size);
    rem = storage_size;
    for (i = 0u; i < element_count; i++) {
        BLOC_I_REQUIRE(rem >= stride, "bloc_pool_init: storage_size too small", BLOC_BOUNDS);
        rem -= stride;
    }
    /*
     * The bytes used by the pool, element_count * stride, again without a multiplication. A
     * counted loop that adds the stride would let the optimizer derive its bound with one, which
     * needs a compiler runtime helper on cores without a multiplier (RV32I, R-03).
     */
    span = storage_size - rem;

    /* Everything is valid: only now write anything. */
    base = (uint8_t *)storage;
    pool->storage = base;
    pool->free_head = (struct bloc_handle *)(void *)base;
    for (off = 0u; off < span; off += stride) {
        b = (struct bloc_handle *)(void *)(base + off);
        b->refcount = 0u;
        b->link.next_free =
            (off + stride < span) ? (struct bloc_handle *)(void *)(base + off + stride) : NULL;
    }

    pool->element_size = element_size;
    pool->block_stride = (bloc_size_t)stride;
    pool->element_count = element_count;
    pool->active_count = 0u;
#if BLOC_STATS
    pool->high_water = 0u;
    pool->alloc_failures = 0u;
#endif
    return BLOC_OK;
}

bloc_status_t bloc_pool_deinit(bloc_pool_t *pool)
{
    BLOC_I_LOCK_DECL(lev);

    BLOC_I_CHECK(pool != NULL, "bloc_pool_deinit: pool is NULL", BLOC_INVALID);
    BLOC_I_LOCK(lev);
    if (pool->storage == NULL) {
        BLOC_I_UNLOCK(lev);
        BLOC_I_FAIL("bloc_pool_deinit: pool is not initialized");
        return BLOC_INVALID;
    }
    if (pool->active_count != 0u) {
        BLOC_I_UNLOCK(lev);
        return BLOC_BUSY;
    }
    pool->storage = NULL;
    pool->free_head = NULL;
    pool->element_count = 0u;
    pool->active_count = 0u;
    BLOC_I_UNLOCK(lev);
    return BLOC_OK;
}

bloc_count_t bloc_pool_free_count(const bloc_pool_t *pool)
{
    bloc_count_t free_count;
    BLOC_I_LOCK_DECL(lev);

    BLOC_I_CHECK(pool != NULL, "bloc_pool_free_count: pool is NULL", 0u);
    BLOC_I_LOCK(lev);
    free_count = (bloc_count_t)(pool->element_count - pool->active_count);
    BLOC_I_UNLOCK(lev);
    return free_count;
}

#if BLOC_STATS
bloc_status_t bloc_pool_get_stats(const bloc_pool_t *pool, bloc_pool_stats_t *out)
{
    BLOC_I_LOCK_DECL(lev);

    BLOC_I_CHECK(pool != NULL, "bloc_pool_get_stats: pool is NULL", BLOC_INVALID);
    BLOC_I_CHECK(out != NULL, "bloc_pool_get_stats: out is NULL", BLOC_INVALID);
    BLOC_I_LOCK(lev);
    if (pool->storage == NULL) {
        BLOC_I_UNLOCK(lev);
        BLOC_I_FAIL("bloc_pool_get_stats: pool is not initialized");
        return BLOC_INVALID;
    }
    out->high_water = pool->high_water;
    out->alloc_failures = pool->alloc_failures;
    BLOC_I_UNLOCK(lev);
    return BLOC_OK;
}
#endif

/* --- Allocation --------------------------------------------------------------------------- */

/*
 * Shared body of bloc_alloc and bloc_calloc. The zero fill is done here, after the lock is
 * released, so that bloc_calloc stays a plain tail call.
 */
static bloc_handle_t bloc_i_alloc(bloc_pool_handle_t pool, bloc_size_t headroom, bool zero)
{
    struct bloc_handle *b;
    size_t off;
    size_t max_off;
#if BLOC_DEBUG
    bool inv_ok;
#endif
    BLOC_I_LOCK_DECL(lev);

    BLOC_I_CHECK(pool != NULL, "bloc_alloc: pool is NULL", NULL);
    BLOC_I_CHECK(pool->storage != NULL, "bloc_alloc: pool is not initialized", NULL);

    /*
     * The offset is the headroom rounded up to BLOC_PAYLOAD_ALIGNMENT, and it must not exceed
     * element_size. That is equivalent to headroom <= element_size rounded down to the
     * alignment, which needs one comparison and cannot overflow size_t for any headroom (the
     * rounding itself would overflow for headroom near SIZE_MAX with a 32-bit bloc_size_t).
     */
    max_off = (size_t)pool->element_size & ~((size_t)BLOC_PAYLOAD_ALIGNMENT - 1u);
    if ((size_t)headroom > max_off) {
        return NULL;
    }
    off = BLOC_ALIGN_UP(headroom, BLOC_PAYLOAD_ALIGNMENT);

    BLOC_I_LOCK(lev);
    b = pool->free_head;
    if (b == NULL) {
#if BLOC_STATS
        if (pool->alloc_failures < BLOC_COUNT_MAX) {
            pool->alloc_failures++;
        }
#endif
        BLOC_I_UNLOCK(lev);
        return NULL;
    }
    pool->free_head = b->link.next_free;
    b->link.pool = pool;
    b->refcount = 1u;
    b->len = 0u;
    b->offset = (bloc_size_t)off;
    pool->active_count++;
#if BLOC_STATS
    if (pool->active_count > pool->high_water) {
        pool->high_water = pool->active_count;
    }
#endif
#if BLOC_DEBUG
    inv_ok = pool->active_count <= pool->element_count;
#endif
    BLOC_I_UNLOCK(lev);
#if BLOC_DEBUG
    if (!inv_ok) {
        BLOC_I_FAIL("bloc_alloc: active_count invariant");
    }
#endif
    if (zero) {
        /*
         * memset returns its destination, and the handle is recovered from it instead of being
         * kept in a register across the call: a value that lives across the call makes GCC for
         * PowerPC emit a libgcc register restore helper, which R-03 does not allow.
         */
        uint8_t *data = (uint8_t *)memset(bloc_i_data_start(b), 0, (size_t)pool->element_size);

        return (bloc_handle_t)(uintptr_t)(data - BLOC_HEADER_SIZE);
    }
    return b;
}

bloc_handle_t bloc_alloc(bloc_pool_handle_t pool, bloc_size_t headroom)
{
    return bloc_i_alloc(pool, headroom, false);
}

bloc_handle_t bloc_calloc(bloc_pool_handle_t pool, bloc_size_t headroom)
{
    return bloc_i_alloc(pool, headroom, true);
}

/* --- Reference counting ------------------------------------------------------------------- */

bloc_status_t bloc_retain(bloc_handle_t b)
{
    BLOC_I_LOCK_DECL(lev);

    BLOC_I_CHECK(b != NULL, "bloc_retain: b is NULL", BLOC_INVALID);
    BLOC_I_LOCK(lev);
    if (b->refcount == 0u) {
        BLOC_I_UNLOCK(lev);
        BLOC_I_FAIL("bloc_retain: block is free");
        return BLOC_INVALID;
    }
#if BLOC_DEBUG
    if (!BLOC_I_ADDR_VALID(b)) {
        BLOC_I_UNLOCK(lev);
        BLOC_I_FAIL("bloc_retain: invalid handle");
        return BLOC_INVALID;
    }
#endif
    if (b->refcount == BLOC_REFCOUNT_MAX) {
        BLOC_I_UNLOCK(lev);
        return BLOC_OVERFLOW;
    }
    b->refcount++;
    BLOC_I_UNLOCK(lev);
    return BLOC_OK;
}

bloc_status_t bloc_release(bloc_handle_t b)
{
    struct bloc_pool *pool;
#if BLOC_DEBUG
    bool inv_ok;
#endif
    BLOC_I_LOCK_DECL(lev);

    if (b == NULL) {
        return BLOC_OK;
    }
    BLOC_I_LOCK(lev);
    if (b->refcount == 0u) {
        BLOC_I_UNLOCK(lev);
        BLOC_I_FAIL("bloc_release: block is free");
        return BLOC_INVALID;
    }
#if BLOC_DEBUG
    if (!BLOC_I_ADDR_VALID(b)) {
        BLOC_I_UNLOCK(lev);
        BLOC_I_FAIL("bloc_release: invalid handle");
        return BLOC_INVALID;
    }
#endif
    if (b->refcount > 1u) {
        b->refcount--;
        BLOC_I_UNLOCK(lev);
        return BLOC_OK;
    }
    pool = b->link.pool; /* read before link.next_free overwrites it */
    b->refcount = 0u;
    b->link.next_free = pool->free_head;
    pool->free_head = b;
    pool->active_count--;
#if BLOC_DEBUG
    inv_ok = pool->active_count <= pool->element_count;
#endif
    BLOC_I_UNLOCK(lev);
#if BLOC_DEBUG
    if (!inv_ok) {
        BLOC_I_FAIL("bloc_release: active_count invariant");
    }
#endif
    return BLOC_OK;
}

/* --- Accessors ---------------------------------------------------------------------------- */

void *bloc_data(bloc_const_handle_t b)
{
    BLOC_I_CHECK(b != NULL, "bloc_data: b is NULL", NULL);
#if BLOC_DEBUG
    BLOC_I_REQUIRE(BLOC_I_HANDLE_VALID(b), "bloc_data: invalid handle", NULL);
#endif
    /* The single place that turns a const handle into a mutable data pointer (spec section 10). */
    return bloc_i_data_start(b) + b->offset;
}

bloc_size_t bloc_len(bloc_const_handle_t b)
{
    BLOC_I_CHECK(b != NULL, "bloc_len: b is NULL", 0u);
#if BLOC_DEBUG
    BLOC_I_REQUIRE(BLOC_I_HANDLE_VALID(b), "bloc_len: invalid handle", 0u);
#endif
    return b->len;
}

bloc_size_t bloc_headroom(bloc_const_handle_t b)
{
    BLOC_I_CHECK(b != NULL, "bloc_headroom: b is NULL", 0u);
#if BLOC_DEBUG
    BLOC_I_REQUIRE(BLOC_I_HANDLE_VALID(b), "bloc_headroom: invalid handle", 0u);
#endif
    return b->offset;
}

bloc_size_t bloc_tailroom(bloc_const_handle_t b)
{
    BLOC_I_CHECK(b != NULL, "bloc_tailroom: b is NULL", 0u);
#if BLOC_DEBUG
    BLOC_I_REQUIRE(BLOC_I_HANDLE_VALID(b), "bloc_tailroom: invalid handle", 0u);
#endif
    return (bloc_size_t)(b->link.pool->element_size - b->offset - b->len);
}

/* --- Length operations -------------------------------------------------------------------- */

bloc_status_t bloc_set_len(bloc_handle_t b, bloc_size_t len)
{
    BLOC_I_CHECK(b != NULL, "bloc_set_len: b is NULL", BLOC_INVALID);
#if BLOC_DEBUG
    BLOC_I_REQUIRE(BLOC_I_HANDLE_VALID(b), "bloc_set_len: invalid handle", BLOC_INVALID);
#endif
    BLOC_I_CHECK((size_t)len <= (size_t)b->link.pool->element_size - b->offset,
                 "bloc_set_len: len exceeds the space after offset", BLOC_BOUNDS);
    BLOC_I_SHARED_MUTATION(b, "bloc_set_len: buffer is shared");
    b->len = len;
    return BLOC_OK;
}

bloc_status_t bloc_add_header(bloc_handle_t b, bloc_size_t n)
{
    BLOC_I_CHECK(b != NULL, "bloc_add_header: b is NULL", BLOC_INVALID);
#if BLOC_DEBUG
    BLOC_I_REQUIRE(BLOC_I_HANDLE_VALID(b), "bloc_add_header: invalid handle", BLOC_INVALID);
#endif
    BLOC_I_CHECK(n <= b->offset, "bloc_add_header: n exceeds headroom", BLOC_BOUNDS);
    BLOC_I_SHARED_MUTATION(b, "bloc_add_header: buffer is shared");
    b->offset = (bloc_size_t)(b->offset - n);
    b->len = (bloc_size_t)(b->len + n);
    return BLOC_OK;
}

bloc_status_t bloc_remove_header(bloc_handle_t b, bloc_size_t n)
{
    BLOC_I_CHECK(b != NULL, "bloc_remove_header: b is NULL", BLOC_INVALID);
#if BLOC_DEBUG
    BLOC_I_REQUIRE(BLOC_I_HANDLE_VALID(b), "bloc_remove_header: invalid handle", BLOC_INVALID);
#endif
    BLOC_I_CHECK(n <= b->len, "bloc_remove_header: n exceeds len", BLOC_BOUNDS);
    BLOC_I_SHARED_MUTATION(b, "bloc_remove_header: buffer is shared");
    b->offset = (bloc_size_t)(b->offset + n);
    b->len = (bloc_size_t)(b->len - n);
    return BLOC_OK;
}
