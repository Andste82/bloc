/*
 * Append tests APP-01..APP-07 (implementation plan, section 8.8): bloc_append and
 * bloc_append_data.
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
#define BUF_MAX ((size_t)512)

/*
 * The largest size value, read through a volatile object: with a constant, link-time optimization
 * proves that the bounds check does not stop the call in the caller's context, and GCC then warns
 * about a read past the end of the test's source buffer on a path that cannot execute.
 */
#if BLOC_CHECKS
static volatile bloc_size_t g_size_max = BLOC_SIZE_MAX;
#define SIZE_MAX_V ((bloc_size_t)g_size_max)
#endif

static ts_snap_t g_snap;
static uint8_t g_src[BUF_MAX];
static uint8_t g_before[BUF_MAX];
static uint8_t g_src_before[BUF_MAX];

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

static void snap_pool(const bloc_pool_t *pool)
{
    ts_snap_take(&g_snap, pool, pool->storage,
                 (size_t)pool->element_count * (size_t)pool->block_stride);
}

static bloc_handle_t alloc_buf(bloc_pool_t *pool, size_t headroom)
{
    bloc_handle_t b = bloc_alloc(pool, (bloc_size_t)headroom);

    TEST_ASSERT_NOT_NULL(b);
    return b;
}

static void release(bloc_handle_t b) { TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b)); }

/* --- APP-01 ------------------------------------------------------------------------------- */

void test_APP_01_append_data_order(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    bloc_handle_t b;
    uint8_t *area;
    size_t off;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    b = alloc_buf(&pool, PA);
    off = bloc_headroom(b);
    area = ts_area(b);
    ts_fill(area, e, 0x80u);
    memcpy(g_before, area, e);
    ts_fill(g_src, 16u, 0x01u);

    /* To an empty buffer. */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append_data(b, g_src, 5u));
    TEST_ASSERT_EQUAL_UINT(5u, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(off, bloc_headroom(b));
    ts_check_fill(area + off, 5u, 0x01u);
    ts_check_same_outside(area, g_before, e, off, off + 5u);

    /* To a non-empty buffer. */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append_data(b, g_src + 5, 7u));
    TEST_ASSERT_EQUAL_UINT(12u, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(off, bloc_headroom(b));
    ts_check_fill(area + off, 12u, 0x01u);
    ts_check_same_outside(area, g_before, e, off, off + 12u);
    release(b);
}

/* --- APP-02 ------------------------------------------------------------------------------- */

void test_APP_02_append_data_bounds(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    bloc_handle_t b;
    size_t room;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    b = alloc_buf(&pool, PA);
    room = bloc_tailroom(b);
    ts_fill(g_src, e + 1u, 0x20u);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append_data(b, g_src, (bloc_size_t)room));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_tailroom(b));
    ts_check_fill((uint8_t *)bloc_data(b), room, 0x20u);
#if BLOC_CHECKS
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 0u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 4u));
    room = bloc_tailroom(b);
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS,
                                        (int)bloc_append_data(b, g_src, (bloc_size_t)(room + 1u))));
    TS_SNAP_CHECK(&g_snap);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_append_data(b, g_src, SIZE_MAX_V)));
    TS_SNAP_CHECK(&g_snap);
    TEST_ASSERT_EQUAL_UINT(4u, bloc_len(b));
#endif
    release(b);
}

/* --- APP-03 ------------------------------------------------------------------------------- */

void test_APP_03_append_from_buffer(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    bloc_handle_t src;
    bloc_handle_t dst;
    size_t dst_off;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    src = alloc_buf(&pool, 2u * PA);
    dst = alloc_buf(&pool, PA);
    dst_off = bloc_headroom(dst);
    ts_fill(ts_area(src), e, 0x90u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(src, 10u));
    ts_fill(g_src, 4u, 0x01u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append_data(dst, g_src, 4u));
    memcpy(g_before, ts_area(dst), e);
    memcpy(g_src_before, ts_area(src), e);

    /* The first n bytes of the source payload, appended after the old payload. */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append(dst, src, 6u));
    TEST_ASSERT_EQUAL_UINT(10u, bloc_len(dst));
    TEST_ASSERT_EQUAL_UINT(dst_off, bloc_headroom(dst));
    ts_check_fill(ts_area(dst) + dst_off, 4u, 0x01u);
    TEST_ASSERT_EQUAL_MEMORY(bloc_data(src), ts_area(dst) + dst_off + 4u, 6u);
    ts_check_same_outside(ts_area(dst), g_before, e, dst_off + 4u, dst_off + 10u);
    TEST_ASSERT_EQUAL_UINT(10u, bloc_len(src));
    TEST_ASSERT_EQUAL_MEMORY(g_src_before, ts_area(src), e);

#if BLOC_CHECKS
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_append(dst, src, 11u)));
    TS_SNAP_CHECK(&g_snap);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_append(dst, src, SIZE_MAX_V)));
    TS_SNAP_CHECK(&g_snap);
    /* n <= src.len but n > tailroom(dst). */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(dst, (bloc_size_t)(e - dst_off - 3u)));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(src, 10u));
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_append(dst, src, 4u)));
    TS_SNAP_CHECK(&g_snap);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append(dst, src, 3u));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_tailroom(dst));
#endif
    release(src);
    release(dst);
}

/* --- APP-04 ------------------------------------------------------------------------------- */

void test_APP_04_append_to_itself(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    bloc_handle_t b;
    const uint8_t *p;
    size_t off;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    b = alloc_buf(&pool, PA);
    off = bloc_headroom(b);
    p = (const uint8_t *)bloc_data(b);
    ts_fill(g_src, 8u, 0xB0u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append_data(b, g_src, 8u));

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append(b, b, 3u));
    TEST_ASSERT_EQUAL_UINT(11u, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(off, bloc_headroom(b));
    ts_check_fill(p, 8u, 0xB0u);
    TEST_ASSERT_EQUAL_MEMORY(p, p + 8, 3u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 8u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append(b, b, 8u));
    TEST_ASSERT_EQUAL_UINT(16u, bloc_len(b));
    TEST_ASSERT_EQUAL_MEMORY(p, p + 8, 8u);
#if BLOC_CHECKS
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_append(b, b, 17u)));
    TS_SNAP_CHECK(&g_snap);
#endif
    release(b);
}

/* --- APP-05 ------------------------------------------------------------------------------- */

void test_APP_05_append_across_pools(void)
{
    bloc_pool_t big;
    bloc_pool_t small;
    const size_t e = ts_element_size_aligned();
    bloc_handle_t src;
    bloc_handle_t dst;
    size_t room;

    ts_pool_setup(&big, 1u, (bloc_size_t)e);
    ts_pool_setup(&small, 1u, (bloc_size_t)(e / 2u));
    src = alloc_buf(&big, 0u);
    dst = alloc_buf(&small, 0u);
    room = bloc_tailroom(dst);
    ts_fill(ts_area(src), e, 0xC0u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(src, (bloc_size_t)(room + 1u)));

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append(dst, src, (bloc_size_t)room));
    TEST_ASSERT_EQUAL_UINT(room, bloc_len(dst));
    TEST_ASSERT_EQUAL_MEMORY(bloc_data(src), bloc_data(dst), room);
#if BLOC_CHECKS
    snap_pool(&small);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_append(dst, src, 1u)));
    TS_SNAP_CHECK(&g_snap);
#endif
    release(src);
    release(dst);
}

/* --- APP-06 ------------------------------------------------------------------------------- */

void test_APP_06_append_zero_length(void)
{
    bloc_pool_t pool;
    bloc_handle_t src;
    bloc_handle_t dst;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    src = alloc_buf(&pool, 0u);
    dst = alloc_buf(&pool, PA);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(dst, 3u));
    snap_pool(&pool);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append_data(dst, g_src, 0u));
    TS_SNAP_CHECK(&g_snap);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append(dst, src, 0u));
    TS_SNAP_CHECK(&g_snap);
    TEST_ASSERT_EQUAL_UINT(3u, bloc_len(dst));
    release(src);
    release(dst);
}

/* --- APP-07 ------------------------------------------------------------------------------- */

#if BLOC_CHECKS
void test_APP_07_append_null(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = alloc_buf(&pool, 0u);
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_append(NULL, b, 0u)));
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_append(b, NULL, 0u)));
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_append_data(NULL, g_src, 0u)));
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_append_data(b, NULL, 0u)));
    TS_SNAP_CHECK(&g_snap);
    release(b);
}
#endif

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_APP_01_append_data_order);
    RUN_TEST(test_APP_02_append_data_bounds);
    RUN_TEST(test_APP_03_append_from_buffer);
    RUN_TEST(test_APP_04_append_to_itself);
    RUN_TEST(test_APP_05_append_across_pools);
    RUN_TEST(test_APP_06_append_zero_length);
#if BLOC_CHECKS
    RUN_TEST(test_APP_07_append_null);
#endif
    return UNITY_END();
}
