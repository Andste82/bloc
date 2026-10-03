/*
 * Reference counting tests REF-01..REF-11 (implementation plan, section 8.4).
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bloc.h"
#include "ts_arena.h"
#include "ts_helpers.h"
#include "ts_snap.h"
#include "unity.h"

static ts_snap_t g_snap;

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

static void snap_pool(const bloc_pool_t *pool)
{
    ts_snap_take(&g_snap, pool, pool->storage,
                 (size_t)pool->element_count * (size_t)pool->block_stride);
}

/* --- REF-01 ------------------------------------------------------------------------------- */

void test_REF_01_retain_and_release(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 3u, 16u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_retain(b));
    TEST_ASSERT_EQUAL_UINT(2u, b->refcount);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_UINT(1u, b->refcount);
    TEST_ASSERT_EQUAL_UINT(2u, bloc_pool_free_count(&pool));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_UINT(0u, b->refcount);
    TEST_ASSERT_EQUAL_UINT(3u, bloc_pool_free_count(&pool));
}

/* --- REF-02 ------------------------------------------------------------------------------- */

#if BLOC_CHECKS
void test_REF_02_retain_null(void)
{
    bloc_status_t r = BLOC_OK;

    TS_CHK_ASSERT(r = bloc_retain(NULL));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
}
#endif

/* --- REF-03 ------------------------------------------------------------------------------- */

void test_REF_03_retain_free_block(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;
    bloc_status_t r = BLOC_OK;

    ts_pool_setup(&pool, 2u, 16u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));

    snap_pool(&pool);
    TS_CHK_ASSERT(r = bloc_retain(b));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    TS_SNAP_CHECK(&g_snap);
    TEST_ASSERT_EQUAL_UINT(0u, b->refcount);
}

/* --- REF-04 ------------------------------------------------------------------------------- */

void test_REF_04_retain_overflow(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, 16u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);

    b->refcount = (bloc_refcount_t)(BLOC_REFCOUNT_MAX - 1u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_retain(b));
    TEST_ASSERT_TRUE(b->refcount == BLOC_REFCOUNT_MAX);
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OVERFLOW, (int)bloc_retain(b)));
    TEST_ASSERT_TRUE(b->refcount == BLOC_REFCOUNT_MAX);

    b->refcount = 1u; /* restore a consistent state */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_UINT(2u, bloc_pool_free_count(&pool));
}

/* --- REF-05 ------------------------------------------------------------------------------- */

void test_REF_05_release_null(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, 16u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);
    snap_pool(&pool);

    TS_EXPECT_NO_ASSERT(TS_LOCKS(0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(NULL))));
    TS_SNAP_CHECK(&g_snap);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

/* --- REF-06, REF-07 ----------------------------------------------------------------------- */

void test_REF_06_release_shared_only_decrements(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, 16u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_retain(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_retain(b));

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_UINT(2u, b->refcount);
    TEST_ASSERT_EQUAL_UINT(1u, pool.active_count);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_UINT(1u, b->refcount);
    TEST_ASSERT_EQUAL_UINT(1u, pool.active_count);
    TEST_ASSERT_EQUAL_PTR(ts_block(&pool, 1u), pool.free_head);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

void test_REF_07_final_release(void)
{
    bloc_pool_t pool;
    bloc_handle_t a;
    bloc_handle_t b;

    ts_pool_setup(&pool, 3u, 16u);
    a = bloc_alloc(&pool, 0u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT(2u, pool.active_count);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(a));
    TEST_ASSERT_EQUAL_UINT(0u, a->refcount);
    TEST_ASSERT_EQUAL_UINT(1u, pool.active_count);
    TEST_ASSERT_EQUAL_PTR(a, pool.free_head);
    TEST_ASSERT_EQUAL_PTR(a, bloc_alloc(&pool, 0u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(a));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_UINT(0u, pool.active_count);
}

/* --- REF-08 ------------------------------------------------------------------------------- */

void test_REF_08_double_release(void)
{
    enum { N = 4 };
    bloc_pool_t pool;
    bloc_handle_t b;
    bloc_handle_t all[N];
    bloc_status_t r = BLOC_OK;
    size_t i;
    size_t k;

    ts_pool_setup(&pool, N, 16u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));

    TS_CHK_ASSERT(r = bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    TEST_ASSERT_EQUAL_UINT(0u, pool.active_count);
    TEST_ASSERT_EQUAL_UINT(N, bloc_pool_free_count(&pool));

    for (i = 0u; i < N; i++) {
        all[i] = bloc_alloc(&pool, 0u);
        TEST_ASSERT_NOT_NULL(all[i]);
        for (k = 0u; k < i; k++) {
            TEST_ASSERT_TRUE(all[i] != all[k]);
        }
    }
    TEST_ASSERT_NULL(bloc_alloc(&pool, 0u));
    for (i = 0u; i < N; i++) {
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(all[i]));
    }
}

/* --- REF-09 ------------------------------------------------------------------------------- */

void test_REF_09_release_never_allocated_block(void)
{
    bloc_pool_t pool;
    bloc_handle_t never;
    bloc_status_t r = BLOC_OK;

    ts_pool_setup(&pool, 3u, 16u);
    never = ts_block(&pool, 2u);
    snap_pool(&pool);

    TS_CHK_ASSERT(r = bloc_release(never));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    TS_SNAP_CHECK(&g_snap);
}

/* --- REF-10 ------------------------------------------------------------------------------- */

void test_REF_10_release_returns_to_own_pool(void)
{
    bloc_pool_t a;
    bloc_pool_t b;
    bloc_handle_t x;
    bloc_handle_t y;

    ts_pool_setup(&a, 2u, 16u);
    ts_pool_setup(&b, 3u, 40u);
    x = bloc_alloc(&a, 0u);
    y = bloc_alloc(&b, 0u);
    TEST_ASSERT_NOT_NULL(x);
    TEST_ASSERT_NOT_NULL(y);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(y));
    TEST_ASSERT_EQUAL_UINT(1u, a.active_count);
    TEST_ASSERT_EQUAL_UINT(0u, b.active_count);
    TEST_ASSERT_EQUAL_PTR(y, b.free_head);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(x));
    TEST_ASSERT_EQUAL_PTR(x, a.free_head);
    TEST_ASSERT_EQUAL_UINT(0u, a.active_count);
}

/* --- REF-11 ------------------------------------------------------------------------------- */

#if TS_HAVE_TRACER
void test_REF_11_lock_usage(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;
    bloc_handle_t free_block;
    bloc_status_t r = BLOC_OK;

    ts_pool_setup(&pool, 3u, 16u);
    b = bloc_alloc(&pool, 0u);
    free_block = ts_block(&pool, 2u);
    TEST_ASSERT_NOT_NULL(b);

    TS_EXPECT_LOCKS(1, r = bloc_retain(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
    TS_EXPECT_LOCKS(1, r = bloc_release(b)); /* shared */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);

    b->refcount = BLOC_REFCOUNT_MAX;
    TS_EXPECT_LOCKS(1, r = bloc_retain(b)); /* overflow */
    TEST_ASSERT_EQUAL_INT(BLOC_OVERFLOW, (int)r);
    b->refcount = 1u;

    TS_CHK_ASSERT(TS_EXPECT_LOCKS(1, r = bloc_retain(free_block)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(1, r = bloc_release(free_block)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);

    TS_EXPECT_LOCKS(1, r = bloc_release(b)); /* final */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);

    TS_EXPECT_LOCKS(0, r = bloc_release(NULL));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
#if BLOC_CHECKS
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(0, r = bloc_retain(NULL)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
#endif
}
#endif

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_REF_01_retain_and_release);
#if BLOC_CHECKS
    RUN_TEST(test_REF_02_retain_null);
#endif
    RUN_TEST(test_REF_03_retain_free_block);
    RUN_TEST(test_REF_04_retain_overflow);
    RUN_TEST(test_REF_05_release_null);
    RUN_TEST(test_REF_06_release_shared_only_decrements);
    RUN_TEST(test_REF_07_final_release);
    RUN_TEST(test_REF_08_double_release);
    RUN_TEST(test_REF_09_release_never_allocated_block);
    RUN_TEST(test_REF_10_release_returns_to_own_pool);
#if TS_HAVE_TRACER
    RUN_TEST(test_REF_11_lock_usage);
#endif
    return UNITY_END();
}
