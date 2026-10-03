#include "ts_snap.h"

#include <string.h>

#include "ts_arena.h"
#include "unity.h"

void ts_snap_take(ts_snap_t *s, const bloc_pool_t *pool, const uint8_t *mem, size_t size)
{
    TEST_ASSERT_TRUE_MESSAGE(size <= TS_SNAP_MAX, "snapshot region too large");
    s->pool_src = pool;
    s->mem = mem;
    s->size = size;
    if (pool != NULL) {
        memcpy(&s->pool, pool, sizeof(s->pool));
    }
    if (size != 0u) {
        memcpy(s->bytes, mem, size);
    }
}

void ts_snap_check_at(const ts_snap_t *s, unsigned line)
{
    if (s->pool_src != NULL && memcmp(&s->pool, s->pool_src, sizeof(s->pool)) != 0) {
        UnityFail("pool object changed", (UNITY_LINE_TYPE)line);
    }
    if (s->size != 0u && memcmp(s->bytes, s->mem, s->size) != 0) {
        UnityFail("storage bytes changed", (UNITY_LINE_TYPE)line);
    }
    if (!ts_arena_guards_ok()) {
        UnityFail("storage guard band changed", (UNITY_LINE_TYPE)line);
    }
}
