/*
 * Length operation tests LEN-01..LEN-09 (implementation plan, section 8.6).
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

static ts_snap_t g_snap;

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

static void snap_pool(const bloc_pool_t *pool)
{
    ts_snap_take(&g_snap, pool, pool->storage,
                 (size_t)pool->element_count * (size_t)pool->block_stride);
}

/* The view of a buffer must satisfy headroom + len + tailroom == element_size. */
static void check_sum(const bloc_pool_t *pool, bloc_handle_t b)
{
    TEST_ASSERT_EQUAL_UINT((size_t)pool->element_size, (size_t)bloc_headroom(b) +
                                                           (size_t)bloc_len(b) +
                                                           (size_t)bloc_tailroom(b));
}

/* A buffer with a non-zero offset, so that add_header and the offset arithmetic are visible. */
static bloc_handle_t alloc_with_headroom(bloc_pool_t *pool, size_t headroom)
{
    bloc_handle_t b;

    ts_pool_setup(pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = bloc_alloc(pool, (bloc_size_t)headroom);
    TEST_ASSERT_NOT_NULL(b);
    return b;
}

/* --- LEN-01 ------------------------------------------------------------------------------- */

void test_LEN_01_set_len_bounds(void)
{
    bloc_pool_t pool;
    bloc_handle_t b = alloc_with_headroom(&pool, 2u * PA);
    const size_t room = (size_t)pool.element_size - (size_t)b->offset;

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, (bloc_size_t)room));
    TEST_ASSERT_EQUAL_UINT(room, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_tailroom(b));
    check_sum(&pool, b);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 0u));

#if BLOC_CHECKS
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 1u));
    snap_pool(&pool);
    TS_CHK_ASSERT(
        TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_set_len(b, (bloc_size_t)(room + 1u))));
    TS_SNAP_CHECK(&g_snap);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_set_len(b, BLOC_SIZE_MAX)));
    TS_SNAP_CHECK(&g_snap);
    TEST_ASSERT_EQUAL_UINT(1u, bloc_len(b));
#endif
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

/* --- LEN-02 ------------------------------------------------------------------------------- */

void test_LEN_02_set_len_shrink(void)
{
    bloc_pool_t pool;
    bloc_handle_t b = alloc_with_headroom(&pool, PA);
    const size_t off = bloc_headroom(b);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 10u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 4u));
    TEST_ASSERT_EQUAL_UINT(4u, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(off, bloc_headroom(b));
    check_sum(&pool, b);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(off, bloc_headroom(b));
    check_sum(&pool, b);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

/* --- LEN-03 ------------------------------------------------------------------------------- */

void test_LEN_03_set_len_keeps_payload(void)
{
    bloc_pool_t pool;
    bloc_handle_t b = alloc_with_headroom(&pool, PA);
    const size_t room = (size_t)pool.element_size - (size_t)b->offset;
    uint8_t *data = (uint8_t *)bloc_data(b);

    ts_fill(data, room, 0x11u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, (bloc_size_t)room));
    ts_check_fill(data, room, 0x11u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 3u));
    ts_check_fill(data, room, 0x11u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 0u));
    ts_check_fill(data, room, 0x11u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

/* --- LEN-04 ------------------------------------------------------------------------------- */

void test_LEN_04_add_header(void)
{
    bloc_pool_t pool;
    bloc_handle_t b = alloc_with_headroom(&pool, 3u * PA);
    const size_t off = bloc_headroom(b);
    const size_t len = 5u;
    uint8_t *data;
    uint8_t *old_data;

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, (bloc_size_t)len));
    old_data = (uint8_t *)bloc_data(b);
    ts_fill(old_data, len, 0x21u);

    snap_pool(&pool);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_add_header(b, 0u));
    TS_SNAP_CHECK(&g_snap);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_add_header(b, (bloc_size_t)off));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_headroom(b));
    TEST_ASSERT_EQUAL_UINT(len + off, bloc_len(b));
    data = (uint8_t *)bloc_data(b);
    TEST_ASSERT_EQUAL_PTR(old_data - off, data);
    ts_check_fill(old_data, len, 0x21u);
    check_sum(&pool, b);

#if BLOC_CHECKS
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_add_header(b, 1u)));
    TS_SNAP_CHECK(&g_snap);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_add_header(b, BLOC_SIZE_MAX)));
    TS_SNAP_CHECK(&g_snap);
    /* n == offset + 1 with a non-zero offset. */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_remove_header(b, 2u));
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_add_header(b, 3u)));
    TS_SNAP_CHECK(&g_snap);
#endif
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

/* --- LEN-05 ------------------------------------------------------------------------------- */

void test_LEN_05_remove_header(void)
{
    bloc_pool_t pool;
    bloc_handle_t b = alloc_with_headroom(&pool, PA);
    const size_t off = bloc_headroom(b);
    const size_t len = 9u;

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, (bloc_size_t)len));
    snap_pool(&pool);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_remove_header(b, 0u));
    TS_SNAP_CHECK(&g_snap);

#if BLOC_CHECKS
    TS_CHK_ASSERT(
        TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_remove_header(b, (bloc_size_t)(len + 1u))));
    TS_SNAP_CHECK(&g_snap);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_remove_header(b, BLOC_SIZE_MAX)));
    TS_SNAP_CHECK(&g_snap);
#endif

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_remove_header(b, (bloc_size_t)len));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(off + len, bloc_headroom(b));
    check_sum(&pool, b);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

/* --- LEN-06 ------------------------------------------------------------------------------- */

void test_LEN_06_remove_then_add_restores_view(void)
{
    bloc_pool_t pool;
    bloc_handle_t b = alloc_with_headroom(&pool, PA);
    const size_t off = bloc_headroom(b);
    const size_t len = 12u;
    void *data;
    size_t k;

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, (bloc_size_t)len));
    data = bloc_data(b);
    for (k = 0u; k <= len; k += 4u) {
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_remove_header(b, (bloc_size_t)k));
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_add_header(b, (bloc_size_t)k));
        TEST_ASSERT_EQUAL_PTR(data, bloc_data(b));
        TEST_ASSERT_EQUAL_UINT(len, bloc_len(b));
        TEST_ASSERT_EQUAL_UINT(off, bloc_headroom(b));
        check_sum(&pool, b);
    }
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

/* --- LEN-07 ------------------------------------------------------------------------------- */

#if BLOC_CHECKS
void test_LEN_07_null_handle(void)
{
    bloc_status_t r = BLOC_OK;

    TS_CHK_ASSERT(r = bloc_set_len(NULL, 1u));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    r = BLOC_OK;
    TS_CHK_ASSERT(r = bloc_add_header(NULL, 1u));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    r = BLOC_OK;
    TS_CHK_ASSERT(r = bloc_remove_header(NULL, 1u));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
}
#endif

/* --- LEN-08 ------------------------------------------------------------------------------- */

#if BLOC_DEBUG
void test_LEN_08_shared_buffer_mutation(void)
{
    bloc_pool_t pool;
    bloc_handle_t b = alloc_with_headroom(&pool, 2u * PA);
    const size_t off = bloc_headroom(b);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_retain(b));
    TEST_ASSERT_EQUAL_UINT(2u, b->refcount);

    TS_EXPECT_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 6u)));
    TEST_ASSERT_EQUAL_UINT(6u, bloc_len(b));
    TS_EXPECT_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_remove_header(b, 2u)));
    TEST_ASSERT_EQUAL_UINT(4u, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(off + 2u, bloc_headroom(b));
    TS_EXPECT_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_add_header(b, 2u)));
    TEST_ASSERT_EQUAL_UINT(6u, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(off, bloc_headroom(b));

    /* Unshared: no assertion. */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 2u)));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}
#endif

/* --- LEN-09 ------------------------------------------------------------------------------- */

#if TS_HAVE_TRACER
void test_LEN_09_no_locking(void)
{
    bloc_pool_t pool;
    bloc_handle_t b = alloc_with_headroom(&pool, PA);

    TS_EXPECT_LOCKS(0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 4u)));
    TS_EXPECT_LOCKS(0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_remove_header(b, 1u)));
    TS_EXPECT_LOCKS(0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_add_header(b, 1u)));
#if BLOC_CHECKS
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(0, (void)bloc_set_len(b, BLOC_SIZE_MAX)));
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(0, (void)bloc_remove_header(b, BLOC_SIZE_MAX)));
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(0, (void)bloc_add_header(b, BLOC_SIZE_MAX)));
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(0, (void)bloc_set_len(NULL, 0u)));
#endif
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}
#endif

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_LEN_01_set_len_bounds);
    RUN_TEST(test_LEN_02_set_len_shrink);
    RUN_TEST(test_LEN_03_set_len_keeps_payload);
    RUN_TEST(test_LEN_04_add_header);
    RUN_TEST(test_LEN_05_remove_header);
    RUN_TEST(test_LEN_06_remove_then_add_restores_view);
#if BLOC_CHECKS
    RUN_TEST(test_LEN_07_null_handle);
#endif
#if BLOC_DEBUG
    RUN_TEST(test_LEN_08_shared_buffer_mutation);
#endif
#if TS_HAVE_TRACER
    RUN_TEST(test_LEN_09_no_locking);
#endif
    return UNITY_END();
}
