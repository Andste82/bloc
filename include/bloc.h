/*
 * BLOC - Buffer Lifetime & Offset Control: public API, types and layout macros.
 *
 * Scaffolding stage: the declarations below are complete, but the functions are
 * not implemented yet and the per-function documentation is still to be written.
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

#define BLOC_VERSION_MAJOR 1
#define BLOC_VERSION_MINOR 0
#define BLOC_VERSION_PATCH 0

typedef BLOC_SIZE_T bloc_size_t;
typedef BLOC_COUNT_T bloc_count_t;
typedef BLOC_REFCOUNT_T bloc_refcount_t;

#define BLOC_SIZE_MAX ((BLOC_SIZE_T) - 1)
#define BLOC_COUNT_MAX ((BLOC_COUNT_T) - 1)
#define BLOC_REFCOUNT_MAX ((BLOC_REFCOUNT_T) - 1)

typedef enum {
    BLOC_OK = 0,
    BLOC_INVALID,  /* NULL, uninitialized pool, free block, bad parameter */
    BLOC_BOUNDS,   /* length, offset or storage size out of range         */
    BLOC_BUSY,     /* pool still has active buffers                       */
    BLOC_OVERFLOW, /* refcount at maximum                                 */
    BLOC_ALIGNMENT /* storage base misaligned                             */
} bloc_status_t;

struct bloc_pool;

struct bloc_handle {
    union {
        struct bloc_pool *pool;        /* allocated: owning pool        */
        struct bloc_handle *next_free; /* free: next free block or NULL */
    } link;
    bloc_size_t offset;       /* headroom = start of payload   */
    bloc_size_t len;          /* payload length in bytes       */
    bloc_refcount_t refcount; /* 0 = free                      */
};

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

typedef struct bloc_pool bloc_pool_t;
typedef bloc_pool_t *bloc_pool_handle_t;
typedef struct bloc_handle *bloc_handle_t;
typedef const struct bloc_handle *bloc_const_handle_t;

#if BLOC_STATS
typedef struct {
    bloc_count_t high_water;
    bloc_count_t alloc_failures;
} bloc_pool_stats_t;
#endif

/* --- Layout macros (all integer constant expressions) ----------------------------------- */

#define BLOC_MAX2(a, b) ((a) > (b) ? (a) : (b))
#define BLOC_MAX3(a, b, c) BLOC_MAX2(BLOC_MAX2((size_t)(a), (size_t)(b)), (size_t)(c))

#define BLOC_STORAGE_ALIGNMENT                                                                     \
    BLOC_MAX3(BLOC_BLOCK_ALIGNMENT, BLOC_PAYLOAD_ALIGNMENT, _Alignof(struct bloc_handle))

#define BLOC_ALIGN_UP(x, a) (((size_t)(x) + ((size_t)(a) - 1u)) & ~((size_t)(a) - 1u))

#define BLOC_HEADER_SIZE BLOC_ALIGN_UP(sizeof(struct bloc_handle), BLOC_PAYLOAD_ALIGNMENT)

#define BLOC_BLOCK_STRIDE(e) BLOC_ALIGN_UP(BLOC_HEADER_SIZE + (size_t)(e), BLOC_STORAGE_ALIGNMENT)

#define BLOC_POOL_SIZE(n, e) ((size_t)(n) * BLOC_BLOCK_STRIDE(e))

#define BLOC_POOL_STORAGE(name, n, e)                                                              \
    _Alignas(BLOC_STORAGE_ALIGNMENT) uint8_t name[BLOC_POOL_SIZE(n, e)]

#define BLOC_ELEMENT_SIZE_MAX                                                                      \
    (((size_t)BLOC_SIZE_MAX & ~((size_t)BLOC_STORAGE_ALIGNMENT - 1u)) - BLOC_HEADER_SIZE)

/* --- Pool lifecycle --------------------------------------------------------------------- */

bloc_status_t bloc_pool_init(bloc_pool_t *pool, void *storage, size_t storage_size,
                             bloc_count_t element_count, bloc_size_t element_size);
bloc_status_t bloc_pool_deinit(bloc_pool_t *pool);
bloc_count_t bloc_pool_free_count(const bloc_pool_t *pool);
#if BLOC_STATS
bloc_status_t bloc_pool_get_stats(const bloc_pool_t *pool, bloc_pool_stats_t *out);
#endif

/* --- Allocation and lifetime ------------------------------------------------------------ */

bloc_handle_t bloc_alloc(bloc_pool_handle_t pool, bloc_size_t headroom);
bloc_handle_t bloc_calloc(bloc_pool_handle_t pool, bloc_size_t headroom);
bloc_status_t bloc_retain(bloc_handle_t b);
bloc_status_t bloc_release(bloc_handle_t b);

/* --- Access and zero-copy length operations --------------------------------------------- */

void *bloc_data(bloc_const_handle_t b);
bloc_size_t bloc_len(bloc_const_handle_t b);
bloc_size_t bloc_headroom(bloc_const_handle_t b);
bloc_size_t bloc_tailroom(bloc_const_handle_t b);
bloc_status_t bloc_set_len(bloc_handle_t b, bloc_size_t len);
bloc_status_t bloc_add_header(bloc_handle_t b, bloc_size_t n);
bloc_status_t bloc_remove_header(bloc_handle_t b, bloc_size_t n);

/* --- Copy, append and prepend ----------------------------------------------------------- */

bloc_status_t bloc_copy_from(bloc_handle_t dst, const void *src, bloc_size_t n);
bloc_status_t bloc_copy_to(bloc_const_handle_t src, void *dst, bloc_size_t n, bloc_size_t pos);
bloc_status_t bloc_copy(bloc_handle_t dst, bloc_const_handle_t src);
bloc_status_t bloc_append(bloc_handle_t dst, bloc_const_handle_t src, bloc_size_t n);
bloc_status_t bloc_append_data(bloc_handle_t dst, const void *src, bloc_size_t n);
bloc_status_t bloc_prepend(bloc_handle_t dst, bloc_const_handle_t src, bloc_size_t n);
bloc_status_t bloc_prepend_data(bloc_handle_t dst, const void *src, bloc_size_t n);

#ifdef __cplusplus
}
#endif

#endif /* BLOC_H */
