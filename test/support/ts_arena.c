#include "ts_arena.h"

#include <string.h>

#include "bloc/bloc.h"
#include "unity.h"

struct ts_region {
    uint8_t *user;
    size_t size;
};

static _Alignas(64) uint8_t ts_arena_mem[TS_ARENA_SIZE];
static struct ts_region ts_regions[TS_ARENA_MAX_REGIONS];
static size_t ts_region_count;
static size_t ts_arena_used;

static size_t ts_align_up(size_t x, size_t a) { return (x + (a - 1u)) & ~(a - 1u); }

uint8_t *ts_storage(size_t size, size_t misalign)
{
    size_t align = BLOC_STORAGE_ALIGNMENT;
    uintptr_t base = (uintptr_t)(void *)ts_arena_mem;
    uintptr_t start;
    uintptr_t user;
    uint8_t *region;

    if (align < TS_ARENA_GUARD) {
        align = TS_ARENA_GUARD;
    }
    if (ts_region_count >= TS_ARENA_MAX_REGIONS || size > TS_ARENA_SIZE ||
        misalign > TS_ARENA_SIZE) {
        return NULL;
    }

    start = base + ts_arena_used + TS_ARENA_GUARD;
    user = (uintptr_t)ts_align_up((size_t)start, align) + misalign;
    if (user - base + size + TS_ARENA_GUARD > TS_ARENA_SIZE) {
        return NULL;
    }

    region = ts_arena_mem + (user - base);
    memset(region - TS_ARENA_GUARD, (int)TS_ARENA_GUARD_BYTE, TS_ARENA_GUARD);
    memset(region, (int)TS_ARENA_FILL_BYTE, size);
    memset(region + size, (int)TS_ARENA_GUARD_BYTE, TS_ARENA_GUARD);

    ts_regions[ts_region_count].user = region;
    ts_regions[ts_region_count].size = size;
    ts_region_count++;
    ts_arena_used = (size_t)(user - base) + size + TS_ARENA_GUARD;
    return region;
}

void ts_arena_reset(void)
{
    ts_region_count = 0u;
    ts_arena_used = 0u;
}

size_t ts_arena_region_count(void) { return ts_region_count; }

static int ts_guard_intact(const uint8_t *p)
{
    size_t i;
    for (i = 0u; i < TS_ARENA_GUARD; i++) {
        if (p[i] != TS_ARENA_GUARD_BYTE) {
            return 0;
        }
    }
    return 1;
}

int ts_arena_guards_ok(void)
{
    size_t i;
    for (i = 0u; i < ts_region_count; i++) {
        const struct ts_region *r = &ts_regions[i];
        if (!ts_guard_intact(r->user - TS_ARENA_GUARD) || !ts_guard_intact(r->user + r->size)) {
            return 0;
        }
    }
    return 1;
}

void ts_arena_check(void)
{
    TEST_ASSERT_TRUE_MESSAGE(ts_arena_guards_ok() != 0, "storage guard band was modified");
}
