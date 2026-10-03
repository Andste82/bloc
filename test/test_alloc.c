/*
 * Allocation tests ALLOC-01..ALLOC-17 (implementation plan, section 8.3).
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

static ts_snap_t g_snap;

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

/* Snapshot of a whole pool: object and every byte of its storage. */
static void snap_pool(const bloc_pool_t *pool)
{
    ts_snap_take(&g_snap, pool, pool->storage,
                 (size_t)pool->element_count * (size_t)pool->block_stride);
}

static uint8_t *data_start(const struct bloc_handle *b)
{
    return (uint8_t *)(uintptr_t)b + ts_expected_header();
}

/* --- ALLOC-01 ----------------------------------------------------------------------------- */

void test_ALLOC_01_alloc_headroom_zero(void)
{
    bloc_pool_t pool;
    const bloc_size_t e = 32u;
    bloc_handle_t b;

    ts_pool_setup(&pool, 3u, e);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_PTR(pool.storage, (uint8_t *)b);
    TEST_ASSERT_EQUAL_UINT(1u, b->refcount);
    TEST_ASSERT_EQUAL_UINT(0u, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_headroom(b));
    TEST_ASSERT_EQUAL_UINT(e, bloc_tailroom(b));
    TEST_ASSERT_EQUAL_PTR((uint8_t *)b + BLOC_HEADER_SIZE, bloc_data(b));
    TEST_ASSERT_EQUAL_PTR(&pool, b->link.pool);
    TEST_ASSERT_EQUAL_UINT(2u, bloc_pool_free_count(&pool));
    TEST_ASSERT_EQUAL_UINT(1u, pool.active_count);
}

/* --- ALLOC-02 ----------------------------------------------------------------------------- */

void test_ALLOC_02_headroom_table(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    const size_t table[] = {0u, 1u, PA - 1u, PA, PA + 1u, 2u * PA + 1u, e - PA, e};
    size_t i;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    for (i = 0u; i < sizeof(table) / sizeof(table[0]); i++) {
        const size_t want = ts_round_up_pa(table[i]);
        bloc_handle_t b = bloc_alloc(&pool, (bloc_size_t)table[i]);

        if (want <= e) {
            TEST_ASSERT_NOT_NULL(b);
            TEST_ASSERT_EQUAL_UINT(want, bloc_headroom(b));
            TEST_ASSERT_EQUAL_UINT(e - want, bloc_tailroom(b));
            TEST_ASSERT_EQUAL_UINT(0u, (size_t)((uintptr_t)bloc_data(b) % PA));
            TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
        } else {
            TEST_ASSERT_NULL(b);
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u, bloc_pool_free_count(&pool));
}

/* --- ALLOC-03 ----------------------------------------------------------------------------- */

void test_ALLOC_03_oversize_headroom(void)
{
    bloc_pool_t pool;
    bloc_pool_t odd;
    const size_t e = ts_element_size_aligned();
    const bloc_size_t odd_e = (bloc_size_t)(PA + 1u);
    bloc_handle_t b = NULL;
    size_t i;
    const size_t table[] = {e + 1u, e + PA, (size_t)BLOC_SIZE_MAX};

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    snap_pool(&pool);
    for (i = 0u; i < sizeof(table) / sizeof(table[0]); i++) {
        TS_EXPECT_NO_ASSERT(TS_LOCKS(0, b = bloc_alloc(&pool, (bloc_size_t)table[i])));
        TEST_ASSERT_NULL(b);
        TS_SNAP_CHECK(&g_snap);
    }

    /* The rounded headroom, not the raw one, must fit: element_size is not a multiple of PA. */
    ts_pool_setup(&odd, 2u, odd_e);
    snap_pool(&odd);
    TS_EXPECT_NO_ASSERT(b = bloc_alloc(&odd, (bloc_size_t)(PA + 1u)));
    if (PA > 1u) {
        TEST_ASSERT_NULL(b);
        TS_SNAP_CHECK(&g_snap);
    } else {
        TEST_ASSERT_NOT_NULL(b);
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    }
    b = bloc_alloc(&odd, (bloc_size_t)PA);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT(PA, bloc_headroom(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

/* --- ALLOC-04 ----------------------------------------------------------------------------- */

void test_ALLOC_04_exhaustion(void)
{
    bloc_pool_t pool;
    bloc_handle_t blocks[5];
    size_t i;

    ts_pool_setup(&pool, 5u, 16u);
    for (i = 0u; i < 5u; i++) {
        blocks[i] = bloc_alloc(&pool, 0u);
        TEST_ASSERT_NOT_NULL(blocks[i]);
    }
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_NULL(bloc_alloc(&pool, 0u)));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_pool_free_count(&pool));
    for (i = 0u; i < 5u; i++) {
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(blocks[i]));
    }
}

/* --- ALLOC-05, ALLOC-06 ------------------------------------------------------------------- */

#if BLOC_CHECKS
void test_ALLOC_05_pool_null(void)
{
    bloc_handle_t b = &(struct bloc_handle){0};

    TS_CHK_ASSERT(b = bloc_alloc(NULL, 0u));
    TEST_ASSERT_NULL(b);
}
#endif

void test_ALLOC_06_uninitialized_pool(void)
{
    static bloc_pool_t zeroed;
    bloc_pool_t pool;
    bloc_handle_t b = &(struct bloc_handle){0};

    memset(&zeroed, 0, sizeof(zeroed));
    TS_CHK_ASSERT(b = bloc_alloc(&zeroed, 0u));
    TEST_ASSERT_NULL(b);
    b = &(struct bloc_handle){0};
    TS_CHK_ASSERT(b = bloc_calloc(&zeroed, 0u));
    TEST_ASSERT_NULL(b);

    ts_pool_setup(&pool, 2u, 16u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_deinit(&pool));
    b = &(struct bloc_handle){0};
    TS_CHK_ASSERT(b = bloc_alloc(&pool, 0u));
    TEST_ASSERT_NULL(b);
    b = &(struct bloc_handle){0};
    TS_CHK_ASSERT(b = bloc_calloc(&pool, 0u));
    TEST_ASSERT_NULL(b);
}

/* --- ALLOC-07 ----------------------------------------------------------------------------- */

void test_ALLOC_07_handles_are_distinct(void)
{
    enum { N = 6 };
    bloc_pool_t pool;
    bloc_handle_t blocks[N];
    size_t i;
    size_t k;

    ts_pool_setup(&pool, N, 21u);
    for (i = 0u; i < N; i++) {
        blocks[i] = bloc_alloc(&pool, 0u);
        TEST_ASSERT_NOT_NULL(blocks[i]);
    }
    for (i = 0u; i < N; i++) {
        for (k = i + 1u; k < N; k++) {
            const uintptr_t a = (uintptr_t)bloc_data(blocks[i]);
            const uintptr_t b = (uintptr_t)bloc_data(blocks[k]);

            TEST_ASSERT_TRUE(blocks[i] != blocks[k]);
            TEST_ASSERT_TRUE(a + 21u <= b || b + 21u <= a);
            /* The whole blocks (header plus data) are disjoint as well. */
            TEST_ASSERT_TRUE(
                (uintptr_t)blocks[i] + ts_expected_stride(21u) <= (uintptr_t)blocks[k] ||
                (uintptr_t)blocks[k] + ts_expected_stride(21u) <= (uintptr_t)blocks[i]);
        }
    }
    for (i = 0u; i < N; i++) {
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(blocks[i]));
    }
}

/* --- ALLOC-08 ----------------------------------------------------------------------------- */

void test_ALLOC_08_full_writes_stay_in_block(void)
{
    enum { N = 5, E = 24 };
    bloc_pool_t pool;
    bloc_handle_t blocks[N];
    struct bloc_handle header[N];
    size_t i;
    size_t k;

    ts_pool_setup(&pool, N, E);
    for (i = 0u; i < N; i++) {
        blocks[i] = bloc_alloc(&pool, 0u);
        TEST_ASSERT_NOT_NULL(blocks[i]);
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(blocks[i], E));
        memcpy(&header[i], blocks[i], sizeof(header[i]));
    }
    for (i = 0u; i < N; i++) {
        ts_fill(bloc_data(blocks[i]), E, (uint8_t)(0x10u * (i + 1u)));
        for (k = 0u; k < N; k++) {
            TEST_ASSERT_EQUAL_MEMORY(&header[k], blocks[k], sizeof(header[k]));
        }
    }
    for (i = 0u; i < N; i++) {
        ts_check_fill(bloc_data(blocks[i]), E, (uint8_t)(0x10u * (i + 1u)));
        TEST_ASSERT_EQUAL_UINT(E, bloc_len(blocks[i]));
        TEST_ASSERT_EQUAL_UINT(0u, bloc_headroom(blocks[i]));
        TEST_ASSERT_EQUAL_UINT(1u, blocks[i]->refcount);
        TEST_ASSERT_EQUAL_PTR(&pool, blocks[i]->link.pool);
    }
    for (i = 0u; i < N; i++) {
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(blocks[i]));
    }
}

/* --- ALLOC-09 ----------------------------------------------------------------------------- */

void test_ALLOC_09_lifo_reuse(void)
{
    bloc_pool_t pool;
    bloc_handle_t a;
    bloc_handle_t b;

    ts_pool_setup(&pool, 3u, 16u);
    a = bloc_alloc(&pool, 0u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(a));
    TEST_ASSERT_EQUAL_PTR(a, bloc_alloc(&pool, 0u));

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(a));
    TEST_ASSERT_EQUAL_PTR(a, bloc_alloc(&pool, 0u));
    TEST_ASSERT_EQUAL_PTR(b, bloc_alloc(&pool, 0u));
}

/* --- ALLOC-10, ALLOC-11 ------------------------------------------------------------------- */

void test_ALLOC_10_calloc_zeroes_data_area(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    bloc_handle_t b;
    bloc_handle_t c;
    size_t i;

    ts_pool_setup(&pool, 1u, (bloc_size_t)e);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);
    memset(bloc_data(b), 0xFF, e);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));

    c = bloc_calloc(&pool, (bloc_size_t)(PA + 1u));
    TEST_ASSERT_EQUAL_PTR(b, c);
    for (i = 0u; i < e; i++) {
        TEST_ASSERT_EQUAL_HEX8(0u, data_start(c)[i]);
    }
    TEST_ASSERT_EQUAL_UINT(0u, bloc_len(c));
    TEST_ASSERT_EQUAL_UINT(ts_round_up_pa(PA + 1u), bloc_headroom(c));
    TEST_ASSERT_EQUAL_UINT(1u, c->refcount);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(c));
}

void test_ALLOC_11_calloc_failures_write_nothing(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    bloc_handle_t b;
    bloc_handle_t c = &(struct bloc_handle){0};

    ts_pool_setup(&pool, 1u, (bloc_size_t)e);
    snap_pool(&pool);

    /* oversize headroom */
    TS_EXPECT_NO_ASSERT(c = bloc_calloc(&pool, BLOC_SIZE_MAX));
    TEST_ASSERT_NULL(c);
    TS_SNAP_CHECK(&g_snap);

    /* empty pool: only the failure counter (STATS) may change, the storage must not */
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);
    ts_snap_take(&g_snap, NULL, pool.storage, (size_t)pool.block_stride * pool.element_count);
    c = &(struct bloc_handle){0};
    TS_EXPECT_NO_ASSERT(c = bloc_calloc(&pool, 0u));
    TEST_ASSERT_NULL(c);
    TS_SNAP_CHECK(&g_snap);
#if BLOC_STATS
    TEST_ASSERT_EQUAL_UINT(1u, pool.alloc_failures);
#endif
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));

#if BLOC_CHECKS
    c = &(struct bloc_handle){0};
    TS_CHK_ASSERT(c = bloc_calloc(NULL, 0u));
    TEST_ASSERT_NULL(c);
#endif
}

/* --- ALLOC-12..14 ------------------------------------------------------------------------- */

#if BLOC_STATS
void test_ALLOC_12_high_water(void)
{
    bloc_pool_t pool;
    bloc_pool_stats_t st;
    bloc_handle_t blocks[6];
    bloc_count_t i;
    bloc_count_t used = 0u;

    ts_pool_setup(&pool, 6u, 16u);
    for (i = 0u; i < 3u; i++) {
        blocks[used] = bloc_alloc(&pool, 0u);
        TEST_ASSERT_NOT_NULL(blocks[used]);
        used++;
    }
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(blocks[--used]));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(blocks[--used]));
    blocks[used++] = bloc_alloc(&pool, 0u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_get_stats(&pool, &st));
    TEST_ASSERT_EQUAL_UINT(3u, st.high_water);
    TEST_ASSERT_EQUAL_UINT(3u, pool.high_water);

    while (used < 6u) {
        blocks[used++] = bloc_alloc(&pool, 0u);
        TEST_ASSERT_NOT_NULL(blocks[used - 1u]);
    }
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_get_stats(&pool, &st));
    TEST_ASSERT_EQUAL_UINT(6u, st.high_water);
    TEST_ASSERT_EQUAL_UINT(0u, st.alloc_failures);
}

void test_ALLOC_13_alloc_failures_saturate(void)
{
    bloc_pool_t pool;
    bloc_pool_stats_t st;
    bloc_handle_t b;

    ts_pool_setup(&pool, 1u, 16u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);

    TEST_ASSERT_NULL(bloc_alloc(&pool, 0u));
    TEST_ASSERT_NULL(bloc_alloc(&pool, 0u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_get_stats(&pool, &st));
    TEST_ASSERT_EQUAL_UINT(2u, st.alloc_failures);

    pool.alloc_failures = (bloc_count_t)(BLOC_COUNT_MAX - 1u);
    TEST_ASSERT_NULL(bloc_alloc(&pool, 0u));
    TEST_ASSERT_EQUAL_UINT(BLOC_COUNT_MAX, pool.alloc_failures);
    TEST_ASSERT_NULL(bloc_alloc(&pool, 0u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_get_stats(&pool, &st));
    TEST_ASSERT_EQUAL_UINT(BLOC_COUNT_MAX, st.alloc_failures);
    TEST_ASSERT_EQUAL_UINT(1u, st.high_water);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

void test_ALLOC_14_reinit_resets_statistics(void)
{
    bloc_pool_t pool;
    bloc_pool_stats_t st;
    const size_t size = BLOC_POOL_SIZE(2, 16);
    bloc_handle_t a;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, 16u);
    a = bloc_alloc(&pool, 0u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NULL(bloc_alloc(&pool, 0u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(a));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_get_stats(&pool, &st));
    TEST_ASSERT_EQUAL_UINT(2u, st.high_water);
    TEST_ASSERT_EQUAL_UINT(1u, st.alloc_failures);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_init(&pool, pool.storage, size, 2u, 16u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_get_stats(&pool, &st));
    TEST_ASSERT_EQUAL_UINT(0u, st.high_water);
    TEST_ASSERT_EQUAL_UINT(0u, st.alloc_failures);
}
#endif

/* --- ALLOC-15 ----------------------------------------------------------------------------- */

void test_ALLOC_15_alignment_of_every_block(void)
{
    enum { N = 9 };
    static const bloc_size_t sizes[] = {1u, 5u, 16u, 33u};
    size_t k;

    for (k = 0u; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        bloc_pool_t pool;
        bloc_handle_t blocks[N];
        size_t i;

        ts_pool_setup(&pool, N, sizes[k]);
        for (i = 0u; i < N; i++) {
            blocks[i] = bloc_alloc(&pool, 0u);
            TEST_ASSERT_NOT_NULL(blocks[i]);
            TEST_ASSERT_EQUAL_UINT(0u, (size_t)((uintptr_t)blocks[i] % SA));
            TEST_ASSERT_EQUAL_UINT(0u, (size_t)((uintptr_t)bloc_data(blocks[i]) % PA));
            TEST_ASSERT_EQUAL_UINT(0u, (size_t)((uintptr_t)data_start(blocks[i]) % PA));
        }
        for (i = 0u; i < N; i++) {
            TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(blocks[i]));
        }
    }
}

/* --- ALLOC-16 ----------------------------------------------------------------------------- */

#if TS_HAVE_TRACER
void test_ALLOC_16_lock_usage(void)
{
    bloc_pool_t pool;
    static bloc_pool_t zeroed;
    bloc_handle_t a = NULL;
    bloc_handle_t b = NULL;
    bloc_handle_t c = NULL;

    ts_pool_setup(&pool, 2u, 16u);
    TS_EXPECT_LOCKS(1, a = bloc_alloc(&pool, 0u));
    TS_EXPECT_LOCKS(1, b = bloc_calloc(&pool, 0u));
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TS_EXPECT_LOCKS(1, c = bloc_alloc(&pool, 0u)); /* empty pool */
    TEST_ASSERT_NULL(c);
    TS_EXPECT_LOCKS(1, c = bloc_calloc(&pool, 0u)); /* empty pool */
    TEST_ASSERT_NULL(c);

    TS_EXPECT_LOCKS(0, c = bloc_alloc(&pool, BLOC_SIZE_MAX)); /* oversize headroom */
    TEST_ASSERT_NULL(c);

#if BLOC_CHECKS
    c = a;
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(0, c = bloc_alloc(NULL, 0u)));
    TEST_ASSERT_NULL(c);
    memset(&zeroed, 0, sizeof(zeroed));
    c = a;
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(0, c = bloc_alloc(&zeroed, 0u)));
    TEST_ASSERT_NULL(c);
#endif
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(a));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}
#endif

/* --- ALLOC-17 ----------------------------------------------------------------------------- */

#if BLOC_DEBUG
void test_ALLOC_17_active_count_invariant(void)
{
    bloc_pool_t pool;
    bloc_handle_t a;
    bloc_handle_t b;
    bloc_handle_t c = NULL;

    ts_pool_setup(&pool, 3u, 16u);
    a = bloc_alloc(&pool, 0u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    pool.active_count = pool.element_count; /* corrupt: one block is still free */
    TS_EXPECT_ASSERT(c = bloc_alloc(&pool, 0u));
    TEST_ASSERT_NOT_NULL(c); /* continue semantics: the handle is still returned */
    pool.active_count = 3u;  /* restore the consistent state */

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(a));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(c));
    TEST_ASSERT_EQUAL_UINT(3u, bloc_pool_free_count(&pool));
}
#endif

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ALLOC_01_alloc_headroom_zero);
    RUN_TEST(test_ALLOC_02_headroom_table);
    RUN_TEST(test_ALLOC_03_oversize_headroom);
    RUN_TEST(test_ALLOC_04_exhaustion);
#if BLOC_CHECKS
    RUN_TEST(test_ALLOC_05_pool_null);
#endif
    RUN_TEST(test_ALLOC_06_uninitialized_pool);
    RUN_TEST(test_ALLOC_07_handles_are_distinct);
    RUN_TEST(test_ALLOC_08_full_writes_stay_in_block);
    RUN_TEST(test_ALLOC_09_lifo_reuse);
    RUN_TEST(test_ALLOC_10_calloc_zeroes_data_area);
    RUN_TEST(test_ALLOC_11_calloc_failures_write_nothing);
#if BLOC_STATS
    RUN_TEST(test_ALLOC_12_high_water);
    RUN_TEST(test_ALLOC_13_alloc_failures_saturate);
    RUN_TEST(test_ALLOC_14_reinit_resets_statistics);
#endif
    RUN_TEST(test_ALLOC_15_alignment_of_every_block);
#if TS_HAVE_TRACER
    RUN_TEST(test_ALLOC_16_lock_usage);
#endif
#if BLOC_DEBUG
    RUN_TEST(test_ALLOC_17_active_count_invariant);
#endif
    return UNITY_END();
}
