/*
 * Pool lifecycle tests POOL-01..POOL-23 (implementation plan, section 8.2).
 *
 * Every pool uses ts_storage() regions with guard bands. Every failing call is wrapped in
 * TS_CHK_ASSERT(): in a BLOC_DEBUG configuration it must raise exactly one assertion, otherwise
 * none. "Unchanged" is proved with snapshots of the pool object and of the storage bytes.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bloc.h"
#include "ts_arena.h"
#include "ts_helpers.h"
#include "ts_snap.h"
#include "unity.h"

#define PA ((size_t)BLOC_PAYLOAD_ALIGNMENT)
#define SA ((size_t)BLOC_STORAGE_ALIGNMENT)

/*
 * Tag SA > 1. BLOC_STORAGE_ALIGNMENT contains _Alignof and cannot be used in #if. It exceeds 1
 * whenever a configured alignment does, and also through the pointer members of struct
 * bloc_handle on every target except AVR, whose pointers have byte alignment.
 */
#if !defined(__AVR__) || BLOC_BLOCK_ALIGNMENT > 1 || BLOC_PAYLOAD_ALIGNMENT > 1
#define TS_SA_GT_1 1
#else
#define TS_SA_GT_1 0
#endif

static ts_snap_t g_snap;

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

/* A pool object filled with a recognizable pattern, standing for "garbage that must survive". */
static void pattern_pool(bloc_pool_t *p) { memset(p, 0xC3, sizeof(*p)); }

/*
 * A freshly initialized pool: every block has refcount 0, block i links to block i + 1 and the
 * last block terminates the free list (spec section 14).
 */
static void check_fresh_chain(const bloc_pool_t *pool, bloc_count_t n)
{
    bloc_count_t i;

    TEST_ASSERT_EQUAL_PTR(ts_block(pool, 0u), pool->free_head);
    for (i = 0u; i < n; i++) {
        const struct bloc_handle *b = ts_block(pool, (size_t)i);

        TEST_ASSERT_EQUAL_UINT(0u, b->refcount);
        if (i + 1u < n) {
            TEST_ASSERT_EQUAL_PTR(ts_block(pool, (size_t)i + 1u), b->link.next_free);
        } else {
            TEST_ASSERT_NULL(b->link.next_free);
        }
    }
}

/* Allocates every block of the pool and checks the ascending address order (POOL-12). */
static void check_ascending_blocks(bloc_pool_t *pool, bloc_count_t n, bloc_size_t e)
{
    bloc_handle_t blocks[255];
    bloc_count_t i;

    check_fresh_chain(pool, n);
    for (i = 0u; i < n; i++) {
        blocks[i] = bloc_alloc(pool, 0u);
        TEST_ASSERT_NOT_NULL(blocks[i]);
        TEST_ASSERT_EQUAL_PTR(pool->storage + (size_t)i * BLOC_BLOCK_STRIDE(e),
                              (uint8_t *)blocks[i]);
        TEST_ASSERT_EQUAL_PTR(pool->storage + (size_t)i * ts_expected_stride(e),
                              (uint8_t *)blocks[i]);
    }
    TEST_ASSERT_NULL(bloc_alloc(pool, 0u));
    for (i = 0u; i < n; i++) {
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(blocks[i]));
    }
    TEST_ASSERT_EQUAL_UINT(n, bloc_pool_free_count(pool));
}

/* --- POOL-01 ------------------------------------------------------------------------------ */

void test_POOL_01_init_valid(void)
{
    bloc_pool_t pool;
    const size_t size = BLOC_POOL_SIZE(4, 20);
    uint8_t *st = ts_storage(size, 0u);

    pattern_pool(&pool);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_init(&pool, st, size, 4u, 20u));
    TEST_ASSERT_EQUAL_UINT(4u, bloc_pool_free_count(&pool));
    TEST_ASSERT_EQUAL_UINT(0u, pool.active_count);
    TEST_ASSERT_EQUAL_PTR(st, pool.storage);
    TEST_ASSERT_EQUAL_PTR(st, pool.free_head);
    TEST_ASSERT_EQUAL_UINT(20u, pool.element_size);
    TEST_ASSERT_EQUAL_UINT(ts_expected_stride(20u), pool.block_stride);
    TEST_ASSERT_EQUAL_UINT(4u, pool.element_count);
    check_fresh_chain(&pool, 4u);
#if BLOC_STATS
    TEST_ASSERT_EQUAL_UINT(0u, pool.high_water);
    TEST_ASSERT_EQUAL_UINT(0u, pool.alloc_failures);
#endif
}

/* --- POOL-02..05 -------------------------------------------------------------------------- */

void test_POOL_02_pool_null(void)
{
    const size_t size = BLOC_POOL_SIZE(3, 16);
    uint8_t *st = ts_storage(size, 0u);
    bloc_status_t r = BLOC_OK;

    ts_snap_take(&g_snap, NULL, st, size);
    TS_CHK_ASSERT(r = bloc_pool_init(NULL, st, size, 3u, 16u));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    TS_SNAP_CHECK(&g_snap);
}

void test_POOL_03_storage_null(void)
{
    bloc_pool_t pool;
    bloc_status_t r = BLOC_OK;

    pattern_pool(&pool);
    ts_snap_take(&g_snap, &pool, NULL, 0u);
    TS_CHK_ASSERT(r = bloc_pool_init(&pool, NULL, BLOC_POOL_SIZE(3, 16), 3u, 16u));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    TS_SNAP_CHECK(&g_snap);
}

void test_POOL_04_zero_count(void)
{
    bloc_pool_t pool;
    const size_t size = BLOC_POOL_SIZE(3, 16);
    uint8_t *st = ts_storage(size, 0u);
    bloc_status_t r = BLOC_OK;

    pattern_pool(&pool);
    ts_snap_take(&g_snap, &pool, st, size);
    TS_CHK_ASSERT(r = bloc_pool_init(&pool, st, size, 0u, 16u));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    TS_SNAP_CHECK(&g_snap);
}

void test_POOL_05_zero_element_size(void)
{
    bloc_pool_t pool;
    const size_t size = BLOC_POOL_SIZE(3, 16);
    uint8_t *st = ts_storage(size, 0u);
    bloc_status_t r = BLOC_OK;

    pattern_pool(&pool);
    ts_snap_take(&g_snap, &pool, st, size);
    TS_CHK_ASSERT(r = bloc_pool_init(&pool, st, size, 3u, 0u));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    TS_SNAP_CHECK(&g_snap);
}

/* --- POOL-06 ------------------------------------------------------------------------------ */

void test_POOL_06_element_size_limit(void)
{
    bloc_pool_t pool;
    uint8_t *st = ts_storage(64u, 0u);
    bloc_status_t r = BLOC_OK;

    pattern_pool(&pool);
    ts_snap_take(&g_snap, &pool, st, 64u);

    /* The maximum passes check 2; the too small storage is then caught by check 4. */
    TS_CHK_ASSERT(r = bloc_pool_init(&pool, st, 64u, 2u, (bloc_size_t)BLOC_ELEMENT_SIZE_MAX));
    TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)r);
    TS_SNAP_CHECK(&g_snap);

    TS_CHK_ASSERT(
        r = bloc_pool_init(&pool, st, 64u, 2u, (bloc_size_t)(BLOC_ELEMENT_SIZE_MAX + 1u)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    TS_SNAP_CHECK(&g_snap);

    TS_CHK_ASSERT(r = bloc_pool_init(&pool, st, 64u, 2u, BLOC_SIZE_MAX));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    TS_SNAP_CHECK(&g_snap);
}

/* --- POOL-07 ------------------------------------------------------------------------------ */

#if TS_SA_GT_1
void test_POOL_07_misaligned_storage(void)
{
    bloc_pool_t pool;
    const size_t size = BLOC_POOL_SIZE(3, 16);
    const size_t shifts[] = {1u, SA / 2u};
    size_t i;

    for (i = 0u; i < sizeof(shifts) / sizeof(shifts[0]); i++) {
        uint8_t *st = ts_storage(size + SA, shifts[i]);
        bloc_status_t r = BLOC_OK;

        pattern_pool(&pool);
        ts_snap_take(&g_snap, &pool, st, size + SA);
        TS_CHK_ASSERT(r = bloc_pool_init(&pool, st, size, 3u, 16u));
        TEST_ASSERT_EQUAL_INT(BLOC_ALIGNMENT, (int)r);
        TS_SNAP_CHECK(&g_snap);
    }
}

#endif /* TS_SA_GT_1 */

/* --- POOL-08 ------------------------------------------------------------------------------ */

void test_POOL_08_storage_size(void)
{
    static const struct {
        bloc_count_t n;
        bloc_size_t e;
    } cases[] = {{1u, 1u}, {1u, 20u}, {3u, 16u}, {5u, 7u}};
    bloc_pool_t pool;
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const size_t need = BLOC_POOL_SIZE(cases[i].n, cases[i].e);
        const size_t sizes[] = {need - 1u, 0u, 1u};
        uint8_t *st = ts_storage(need, 0u);
        size_t k;

        TEST_ASSERT_EQUAL_UINT((size_t)cases[i].n * ts_expected_stride(cases[i].e), need);
        for (k = 0u; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
            bloc_status_t r = BLOC_OK;

            pattern_pool(&pool);
            ts_snap_take(&g_snap, &pool, st, need);
            TS_CHK_ASSERT(r = bloc_pool_init(&pool, st, sizes[k], cases[i].n, cases[i].e));
            TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)r);
            TS_SNAP_CHECK(&g_snap);
        }
        TEST_ASSERT_EQUAL_INT(BLOC_OK,
                              (int)bloc_pool_init(&pool, st, need, cases[i].n, cases[i].e));
        TEST_ASSERT_EQUAL_UINT(cases[i].n, bloc_pool_free_count(&pool));
    }
}

/* --- POOL-09 ------------------------------------------------------------------------------ */

void test_POOL_09_huge_storage_size(void)
{
    bloc_pool_t pool;
    const size_t need = BLOC_POOL_SIZE(4, 20);
    uint8_t *st = ts_storage(need, 0u);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_init(&pool, st, SIZE_MAX, 4u, 20u));
    TEST_ASSERT_EQUAL_UINT(4u, bloc_pool_free_count(&pool));
    TEST_ASSERT_TRUE(ts_arena_guards_ok() != 0);
}

/* --- POOL-10 ------------------------------------------------------------------------------ */

void test_POOL_10_surplus_storage_untouched(void)
{
    bloc_pool_t pool;
    const bloc_count_t n = 3u;
    const bloc_size_t e = 20u;
    const size_t need = BLOC_POOL_SIZE(3, 20);
    const size_t size = need + 3u * SA;
    uint8_t *st = ts_storage(size, 0u);
    bloc_handle_t blocks[3];
    size_t i;

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_init(&pool, st, size, n, e));
    for (i = 0u; i < n; i++) {
        blocks[i] = bloc_alloc(&pool, 0u);
        TEST_ASSERT_NOT_NULL(blocks[i]);
        memset(bloc_data(blocks[i]), 0xEE, (size_t)bloc_tailroom(blocks[i]));
    }
    for (i = need; i < size; i++) {
        TEST_ASSERT_EQUAL_HEX8(TS_ARENA_FILL_BYTE, st[i]);
    }
    for (i = 0u; i < n; i++) {
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(blocks[i]));
    }
    for (i = need; i < size; i++) {
        TEST_ASSERT_EQUAL_HEX8(TS_ARENA_FILL_BYTE, st[i]);
    }
}

/* --- POOL-11 ------------------------------------------------------------------------------ */

void test_POOL_11_check_order(void)
{
    bloc_pool_t pool;
    bloc_status_t r = BLOC_OK;
    uint8_t *aligned = ts_storage(64u, 0u);
    uint8_t *misaligned = ts_storage(64u, 1u);

    /* (storage == NULL and count == 0) -> INVALID */
    pattern_pool(&pool);
    TS_CHK_ASSERT(r = bloc_pool_init(&pool, NULL, 64u, 0u, 16u));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);

    /* (element too large and misaligned) -> INVALID */
    TS_CHK_ASSERT(r = bloc_pool_init(&pool, misaligned, 64u, 2u, BLOC_SIZE_MAX));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);

    /* (misaligned and too small) -> ALIGNMENT */
    TS_CHK_ASSERT(r = bloc_pool_init(&pool, misaligned, 1u, 2u, 16u));
    TEST_ASSERT_EQUAL_INT(BLOC_ALIGNMENT, (int)r);

    /* (aligned and too small) -> BOUNDS, to show that the order is not accidental */
    TS_CHK_ASSERT(r = bloc_pool_init(&pool, aligned, 1u, 2u, 16u));
    TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)r);
}

/* --- POOL-12 ------------------------------------------------------------------------------ */

void test_POOL_12_blocks_ascend(void)
{
    static const struct {
        bloc_count_t n;
        bloc_size_t e;
    } cases[] = {{1u, 1u}, {2u, 9u}, {6u, 20u}, {5u, 64u}};
    bloc_pool_t pool;
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ts_pool_setup(&pool, cases[i].n, cases[i].e);
        check_ascending_blocks(&pool, cases[i].n, cases[i].e);
    }
}

/* --- POOL-13 ------------------------------------------------------------------------------ */

#if !defined(BLOC_CONFIG_HEADER)
void test_POOL_13_max_element_count(void)
{
    bloc_pool_t pool;

    ts_pool_setup(&pool, BLOC_COUNT_MAX, 1u);
    TEST_ASSERT_EQUAL_UINT(BLOC_COUNT_MAX, bloc_pool_free_count(&pool));
    check_ascending_blocks(&pool, BLOC_COUNT_MAX, 1u);
}
#endif

/* --- POOL-14 ------------------------------------------------------------------------------ */

void test_POOL_14_failed_init_leaves_pool_unchanged(void)
{
    const size_t need = BLOC_POOL_SIZE(3, 16);
    uint8_t *aligned = ts_storage(need, 0u);
    uint8_t *misaligned = ts_storage(need, 1u);
#if TS_SA_GT_1
    uint8_t *half_misaligned = ts_storage(need, SA / 2u);
#endif
    const struct {
        uint8_t *storage;
        size_t size;
        bloc_count_t n;
        bloc_size_t e;
        bloc_status_t expected;
    } cases[] = {
        {NULL, need, 3u, 16u, BLOC_INVALID},
        {aligned, need, 0u, 16u, BLOC_INVALID},
        {aligned, need, 3u, 0u, BLOC_INVALID},
        {aligned, need, 3u, (bloc_size_t)(BLOC_ELEMENT_SIZE_MAX + 1u), BLOC_INVALID},
        {aligned, need, 3u, BLOC_SIZE_MAX, BLOC_INVALID},
        {aligned, need, 3u, (bloc_size_t)BLOC_ELEMENT_SIZE_MAX, BLOC_BOUNDS},
        {misaligned, need, 3u, 16u, BLOC_ALIGNMENT},
#if TS_SA_GT_1
        {half_misaligned, need, 3u, 16u, BLOC_ALIGNMENT},
#endif
        {aligned, need - 1u, 3u, 16u, BLOC_BOUNDS},
        {aligned, 0u, 3u, 16u, BLOC_BOUNDS},
    };
    bloc_pool_t pool;
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        bloc_status_t r = BLOC_OK;

        pattern_pool(&pool);
        /* Snapshot the region that is passed in (the aligned one for the NULL case). */
        ts_snap_take(&g_snap, &pool, cases[i].storage != NULL ? cases[i].storage : aligned, need);
        TS_CHK_ASSERT(
            r = bloc_pool_init(&pool, cases[i].storage, cases[i].size, cases[i].n, cases[i].e));
        TEST_ASSERT_EQUAL_INT((int)cases[i].expected, (int)r);
        TS_SNAP_CHECK(&g_snap);
    }
}

/* --- POOL-15 ------------------------------------------------------------------------------ */

void test_POOL_15_deinit_idle_pool(void)
{
    bloc_pool_t pool;
    const size_t size = BLOC_POOL_SIZE(3, 16);
    uint8_t *st = ts_storage(size, 0u);
    bloc_handle_t b = NULL;

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_init(&pool, st, size, 3u, 16u));
    ts_snap_take(&g_snap, NULL, st, size);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_deinit(&pool));
    TEST_ASSERT_NULL(pool.storage);
    TEST_ASSERT_NULL(pool.free_head);
    TEST_ASSERT_EQUAL_UINT(0u, pool.element_count);
    TEST_ASSERT_EQUAL_UINT(0u, pool.active_count);
    TEST_ASSERT_EQUAL_UINT(0u, bloc_pool_free_count(&pool));
    TS_CHK_ASSERT(b = bloc_alloc(&pool, 0u));
    TEST_ASSERT_NULL(b);
    TS_SNAP_CHECK(&g_snap);
}

/* --- POOL-16 ------------------------------------------------------------------------------ */

void test_POOL_16_deinit_busy_pool(void)
{
    bloc_pool_t pool;
    bloc_pool_t before;
    bloc_handle_t b;

    ts_pool_setup(&pool, 3u, 16u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);
    memcpy(&before, &pool, sizeof(pool));

    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BUSY, (int)bloc_pool_deinit(&pool)));
    TEST_ASSERT_EQUAL_MEMORY(&before, &pool, sizeof(pool));
    TEST_ASSERT_EQUAL_UINT(1u, b->refcount);
    TEST_ASSERT_EQUAL_UINT(0u, bloc_len(b));

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_deinit(&pool));
}

/* --- POOL-17 ------------------------------------------------------------------------------ */

void test_POOL_17_deinit_invalid(void)
{
    bloc_pool_t pool;
    bloc_status_t r = BLOC_OK;

#if BLOC_CHECKS
    TS_CHK_ASSERT(r = bloc_pool_deinit(NULL));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
#endif

    ts_pool_setup(&pool, 2u, 8u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_deinit(&pool));
    TS_CHK_ASSERT(r = bloc_pool_deinit(&pool));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
}

/* --- POOL-18 ------------------------------------------------------------------------------ */

void test_POOL_18_reinit_with_new_geometry(void)
{
    bloc_pool_t pool;
    const size_t size = BLOC_POOL_SIZE(6, 40);
    uint8_t *st = ts_storage(size, 0u);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_init(&pool, st, size, 6u, 40u));
    check_ascending_blocks(&pool, 6u, 40u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_deinit(&pool));

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_init(&pool, st, size, 4u, 12u));
    TEST_ASSERT_EQUAL_UINT(12u, pool.element_size);
    TEST_ASSERT_EQUAL_UINT(4u, pool.element_count);
    check_ascending_blocks(&pool, 4u, 12u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_deinit(&pool));

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_init(&pool, st, size, 2u, 100u));
    check_ascending_blocks(&pool, 2u, 100u);
}

/* --- POOL-19 ------------------------------------------------------------------------------ */

void test_POOL_19_zeroed_pool_is_uninitialized(void)
{
    static bloc_pool_t zeroed;
    bloc_handle_t b = NULL;
    bloc_status_t r = BLOC_OK;

    memset(&zeroed, 0, sizeof(zeroed));
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_UINT(0u, bloc_pool_free_count(&zeroed)));
    TS_CHK_ASSERT(b = bloc_alloc(&zeroed, 0u));
    TEST_ASSERT_NULL(b);
    TS_CHK_ASSERT(r = bloc_pool_deinit(&zeroed));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
}

/* --- POOL-20 ------------------------------------------------------------------------------ */

#if BLOC_CHECKS
void test_POOL_20_free_count_null(void)
{
    bloc_count_t n = 1u;

    TS_CHK_ASSERT(n = bloc_pool_free_count(NULL));
    TEST_ASSERT_EQUAL_UINT(0u, n);
}
#endif

/* --- POOL-21 ------------------------------------------------------------------------------ */

void test_POOL_21_pools_are_independent(void)
{
    bloc_pool_t a;
    bloc_pool_t b;
    const size_t size_a = BLOC_POOL_SIZE(3, 16);
    const size_t size_b = BLOC_POOL_SIZE(5, 40);
    uint8_t *st_a = ts_storage(size_a, 0u);
    uint8_t *st_b = ts_storage(size_b, 0u);
    bloc_handle_t x;
    bloc_handle_t y;
    bloc_handle_t z;

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_init(&a, st_a, size_a, 3u, 16u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_init(&b, st_b, size_b, 5u, 40u));

    ts_snap_take(&g_snap, &b, st_b, size_b);
    x = bloc_alloc(&a, 0u);
    y = bloc_alloc(&a, 0u);
    TEST_ASSERT_NOT_NULL(x);
    TEST_ASSERT_NOT_NULL(y);
    memset(bloc_data(x), 0x11, (size_t)bloc_tailroom(x));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(x));
    TS_SNAP_CHECK(&g_snap);

    ts_snap_take(&g_snap, &a, st_a, size_a);
    z = bloc_alloc(&b, 0u);
    TEST_ASSERT_NOT_NULL(z);
    memset(bloc_data(z), 0x22, (size_t)bloc_tailroom(z));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(z));
    TS_SNAP_CHECK(&g_snap);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(y));
    TEST_ASSERT_EQUAL_UINT(3u, bloc_pool_free_count(&a));
    TEST_ASSERT_EQUAL_UINT(5u, bloc_pool_free_count(&b));
}

/* --- POOL-22 ------------------------------------------------------------------------------ */

#if TS_HAVE_TRACER
void test_POOL_22_lock_usage(void)
{
    bloc_pool_t pool;
    const size_t size = BLOC_POOL_SIZE(3, 16);
    uint8_t *st = ts_storage(size, 0u);
    bloc_handle_t b = NULL;
    bloc_status_t r = BLOC_OK;
    bloc_count_t n = 0u;

    TS_EXPECT_LOCKS(0, r = bloc_pool_init(&pool, st, size, 3u, 16u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
    TS_EXPECT_LOCKS(1, n = bloc_pool_free_count(&pool));
    TEST_ASSERT_EQUAL_UINT(3u, n);

    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);
    TS_EXPECT_LOCKS(1, r = bloc_pool_deinit(&pool));
    TEST_ASSERT_EQUAL_INT(BLOC_BUSY, (int)r);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));

    TS_EXPECT_LOCKS(1, r = bloc_pool_deinit(&pool));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(1, r = bloc_pool_deinit(&pool)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
}
#endif

/* --- POOL-23 ------------------------------------------------------------------------------ */

#if BLOC_STATS
void test_POOL_23_get_stats(void)
{
    bloc_pool_t pool;
    static bloc_pool_t zeroed;
    bloc_pool_stats_t out;
    bloc_status_t r = BLOC_OK;

    ts_pool_setup(&pool, 3u, 16u);

    TS_CHK_ASSERT(r = bloc_pool_get_stats(NULL, &out));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    TS_CHK_ASSERT(r = bloc_pool_get_stats(&pool, NULL));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    memset(&zeroed, 0, sizeof(zeroed));
    TS_CHK_ASSERT(r = bloc_pool_get_stats(&zeroed, &out));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);

    out.high_water = BLOC_COUNT_MAX;
    out.alloc_failures = BLOC_COUNT_MAX;
    TS_EXPECT_NO_ASSERT(r = bloc_pool_get_stats(&pool, &out));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
    TEST_ASSERT_EQUAL_UINT(0u, out.high_water);
    TEST_ASSERT_EQUAL_UINT(0u, out.alloc_failures);
}
#endif

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_POOL_01_init_valid);
    RUN_TEST(test_POOL_02_pool_null);
    RUN_TEST(test_POOL_03_storage_null);
    RUN_TEST(test_POOL_04_zero_count);
    RUN_TEST(test_POOL_05_zero_element_size);
    RUN_TEST(test_POOL_06_element_size_limit);
#if TS_SA_GT_1
    RUN_TEST(test_POOL_07_misaligned_storage);
#endif
    RUN_TEST(test_POOL_08_storage_size);
    RUN_TEST(test_POOL_09_huge_storage_size);
    RUN_TEST(test_POOL_10_surplus_storage_untouched);
    RUN_TEST(test_POOL_11_check_order);
    RUN_TEST(test_POOL_12_blocks_ascend);
#if !defined(BLOC_CONFIG_HEADER)
    RUN_TEST(test_POOL_13_max_element_count);
#endif
    RUN_TEST(test_POOL_14_failed_init_leaves_pool_unchanged);
    RUN_TEST(test_POOL_15_deinit_idle_pool);
    RUN_TEST(test_POOL_16_deinit_busy_pool);
    RUN_TEST(test_POOL_17_deinit_invalid);
    RUN_TEST(test_POOL_18_reinit_with_new_geometry);
    RUN_TEST(test_POOL_19_zeroed_pool_is_uninitialized);
#if BLOC_CHECKS
    RUN_TEST(test_POOL_20_free_count_null);
#endif
    RUN_TEST(test_POOL_21_pools_are_independent);
#if TS_HAVE_TRACER
    RUN_TEST(test_POOL_22_lock_usage);
#endif
#if BLOC_STATS
    RUN_TEST(test_POOL_23_get_stats);
#endif
    return UNITY_END();
}
