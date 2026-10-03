/* Snapshot of a pool object and its storage, to prove that a call changed nothing. */
#ifndef TS_SNAP_H
#define TS_SNAP_H

#include <stddef.h>
#include <stdint.h>

#include "bloc.h"

#define TS_SNAP_MAX ((size_t)8192u)

typedef struct {
    bloc_pool_t pool;
    const bloc_pool_t *pool_src; /* NULL: no pool object is compared */
    const uint8_t *mem;
    size_t size;
    uint8_t bytes[TS_SNAP_MAX];
} ts_snap_t;

/*
 * Records the pool object (if pool is not NULL) and the bytes mem[0..size). size must not exceed
 * TS_SNAP_MAX. Snapshots are large, so declare them static.
 */
void ts_snap_take(ts_snap_t *s, const bloc_pool_t *pool, const uint8_t *mem, size_t size);

/* Fails the running Unity test if the pool object, the bytes or a guard band changed. */
void ts_snap_check_at(const ts_snap_t *s, unsigned line);

#define TS_SNAP_CHECK(s) ts_snap_check_at((s), (unsigned)__LINE__)

#endif /* TS_SNAP_H */
