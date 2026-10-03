/*
 * Prepend tests PRE-01..PRE-09 (implementation plan, section 8.9): bloc_prepend and
 * bloc_prepend_data.
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

/* --- PRE-01 ------------------------------------------------------------------------------- */

void test_PRE_01_prepend_data_into_headroom(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    bloc_handle_t b;
    uint8_t *area;
    uint8_t *old_data;
    size_t off;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    b = alloc_buf(&pool, 4u * PA);
    off = bloc_headroom(b);
    area = ts_area(b);
    ts_fill(area, e, 0x40u);
    ts_fill(g_src, 64u, 0x01u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 6u));
    old_data = (uint8_t *)bloc_data(b);
    memcpy(g_before, area, e);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend_data(b, g_src, (bloc_size_t)(2u * PA)));
    TEST_ASSERT_EQUAL_UINT(off - 2u * PA, bloc_headroom(b));
    TEST_ASSERT_EQUAL_UINT(6u + 2u * PA, bloc_len(b));
    TEST_ASSERT_EQUAL_PTR(old_data - 2u * PA, bloc_data(b));
    TEST_ASSERT_EQUAL_MEMORY(g_src, bloc_data(b), 2u * PA);
    /* The old payload did not move and is intact; the remaining headroom is untouched. */
    TEST_ASSERT_EQUAL_MEMORY(g_before + off, old_data, 6u);
    ts_check_same_outside(area, g_before, e, off - 2u * PA, off);
    release(b);
}

/* --- PRE-02 ------------------------------------------------------------------------------- */

void test_PRE_02_prepend_data_bounds(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;
    size_t room;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = alloc_buf(&pool, 3u * PA);
    room = bloc_headroom(b);
    ts_fill(g_src, 2u * BUF_MAX / 4u, 0x20u);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend_data(b, g_src, (bloc_size_t)room));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_headroom(b));
    TEST_ASSERT_EQUAL_UINT(room, bloc_len(b));
    ts_check_fill((uint8_t *)bloc_data(b), room, 0x20u);
#if BLOC_CHECKS
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_remove_header(b, 1u));
    room = bloc_headroom(b);
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(
        BLOC_BOUNDS, (int)bloc_prepend_data(b, g_src, (bloc_size_t)(room + 1u))));
    TS_SNAP_CHECK(&g_snap);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_prepend_data(b, g_src, SIZE_MAX_V)));
    TS_SNAP_CHECK(&g_snap);
#endif
    release(b);
}

/* --- PRE-03 ------------------------------------------------------------------------------- */

#if BLOC_CHECKS
void test_PRE_03_no_headroom(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = alloc_buf(&pool, 0u);
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_prepend_data(b, g_src, 1u)));
    TS_SNAP_CHECK(&g_snap);
    release(b);
}
#endif

/* --- PRE-04 ------------------------------------------------------------------------------- */

void test_PRE_04_prepend_from_buffer(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    bloc_handle_t src;
    bloc_handle_t dst;
    size_t dst_off;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    src = alloc_buf(&pool, PA);
    dst = alloc_buf(&pool, 4u * PA);
    dst_off = bloc_headroom(dst);
    ts_fill(ts_area(src), e, 0x90u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(src, 10u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(dst, 4u));
    memcpy(g_before, ts_area(dst), e);
    memcpy(g_src_before, ts_area(src), e);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend(dst, src, 3u));
    TEST_ASSERT_EQUAL_UINT(dst_off - 3u, bloc_headroom(dst));
    TEST_ASSERT_EQUAL_UINT(7u, bloc_len(dst));
    TEST_ASSERT_EQUAL_MEMORY(bloc_data(src), bloc_data(dst), 3u);
    TEST_ASSERT_EQUAL_MEMORY(g_before + dst_off, (uint8_t *)bloc_data(dst) + 3, 4u);
    ts_check_same_outside(ts_area(dst), g_before, e, dst_off - 3u, dst_off);
    TEST_ASSERT_EQUAL_UINT(10u, bloc_len(src));
    TEST_ASSERT_EQUAL_MEMORY(g_src_before, ts_area(src), e);

#if BLOC_CHECKS
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_prepend(dst, src, 11u)));
    TS_SNAP_CHECK(&g_snap);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_prepend(dst, src, SIZE_MAX_V)));
    TS_SNAP_CHECK(&g_snap);
    /* n <= src.len but n > headroom(dst). */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(src, (bloc_size_t)(bloc_headroom(dst) + 1u)));
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(
        BLOC_BOUNDS, (int)bloc_prepend(dst, src, (bloc_size_t)(bloc_headroom(dst) + 1u))));
    TS_SNAP_CHECK(&g_snap);
#endif
    release(src);
    release(dst);
}

/* --- PRE-05 ------------------------------------------------------------------------------- */

void test_PRE_05_prepend_to_itself(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;
    const uint8_t *old_data;
    size_t off;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = alloc_buf(&pool, 4u * PA);
    off = bloc_headroom(b);
    old_data = (const uint8_t *)bloc_data(b);
    ts_fill(g_src, 8u, 0xB0u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append_data(b, g_src, 8u));

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend(b, b, 3u));
    TEST_ASSERT_EQUAL_UINT(11u, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(off - 3u, bloc_headroom(b));
    TEST_ASSERT_EQUAL_PTR(old_data - 3, bloc_data(b));
    TEST_ASSERT_EQUAL_MEMORY(old_data, old_data - 3, 3u);
    ts_check_fill(old_data, 8u, 0xB0u);
    release(b);
}

/* --- PRE-06 ------------------------------------------------------------------------------- */

void test_PRE_06_prepend_across_pools(void)
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
    dst = alloc_buf(&small, 2u * PA);
    room = bloc_headroom(dst);
    ts_fill(ts_area(src), e, 0xC0u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(src, (bloc_size_t)(room + 1u)));

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend(dst, src, (bloc_size_t)room));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_headroom(dst));
    TEST_ASSERT_EQUAL_UINT(room, bloc_len(dst));
    TEST_ASSERT_EQUAL_MEMORY(bloc_data(src), bloc_data(dst), room);
#if BLOC_CHECKS
    snap_pool(&small);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_prepend(dst, src, 1u)));
    TS_SNAP_CHECK(&g_snap);
#endif
    release(src);
    release(dst);
}

/* --- PRE-07 ------------------------------------------------------------------------------- */

void test_PRE_07_prepend_zero_length(void)
{
    bloc_pool_t pool;
    bloc_handle_t src;
    bloc_handle_t dst;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    src = alloc_buf(&pool, 0u);
    dst = alloc_buf(&pool, 2u * PA);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(dst, 3u));
    snap_pool(&pool);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend_data(dst, g_src, 0u));
    TS_SNAP_CHECK(&g_snap);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend(dst, src, 0u));
    TS_SNAP_CHECK(&g_snap);
    TEST_ASSERT_EQUAL_UINT(3u, bloc_len(dst));
    release(src);
    release(dst);
}

/* --- PRE-08 ------------------------------------------------------------------------------- */

#if BLOC_CHECKS
void test_PRE_08_prepend_null(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = alloc_buf(&pool, 0u);
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_prepend(NULL, b, 0u)));
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_prepend(b, NULL, 0u)));
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_prepend_data(NULL, g_src, 0u)));
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_prepend_data(b, NULL, 0u)));
    TS_SNAP_CHECK(&g_snap);
    release(b);
}
#endif

/* --- PRE-09 ------------------------------------------------------------------------------- */

/* The TX example of spec section 10: reserve room for two headers, write the payload, then add
 * both headers in place. */
void test_PRE_09_protocol_scenario(void)
{
    bloc_pool_t pool;
    const size_t head = ts_round_up_pa(40u);
    const size_t e = head + 32u;
    const uint8_t payload[16] = {1u, 2u,  3u,  4u,  5u,  6u,  7u,  8u,
                                 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u};
    uint8_t expected[40 + sizeof(payload)];
    bloc_handle_t t;
    const uint8_t *payload_start;
    uint8_t *p;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    t = alloc_buf(&pool, 40u);
    TEST_ASSERT_EQUAL_UINT(head, bloc_headroom(t));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append_data(t, payload, (bloc_size_t)sizeof(payload)));
    payload_start = (const uint8_t *)bloc_data(t);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_add_header(t, 20u));
    memset(bloc_data(t), 0x77, 20u); /* "TCP header" */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_add_header(t, 20u));
    memset(bloc_data(t), 0x66, 20u); /* "IP header" */

    memset(expected, 0x66, 20u);
    memset(expected + 20, 0x77, 20u);
    memcpy(expected + 40, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_UINT(sizeof(expected), bloc_len(t));
    p = (uint8_t *)bloc_data(t);
    TEST_ASSERT_EQUAL_PTR(payload_start - 40, p);
    TEST_ASSERT_EQUAL_MEMORY(expected, p, sizeof(expected));
    release(t);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_PRE_01_prepend_data_into_headroom);
    RUN_TEST(test_PRE_02_prepend_data_bounds);
#if BLOC_CHECKS
    RUN_TEST(test_PRE_03_no_headroom);
#endif
    RUN_TEST(test_PRE_04_prepend_from_buffer);
    RUN_TEST(test_PRE_05_prepend_to_itself);
    RUN_TEST(test_PRE_06_prepend_across_pools);
    RUN_TEST(test_PRE_07_prepend_zero_length);
#if BLOC_CHECKS
    RUN_TEST(test_PRE_08_prepend_null);
#endif
    RUN_TEST(test_PRE_09_protocol_scenario);
    return UNITY_END();
}
