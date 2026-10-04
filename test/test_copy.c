/*
 * Copy tests CPY-01..CPY-12 (implementation plan, section 8.7): bloc_copy_from, bloc_copy_to and
 * bloc_copy.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bloc/bloc.h"
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
static volatile bloc_size_t g_size_max = BLOC_SIZE_MAX;
#define SIZE_MAX_V ((bloc_size_t)g_size_max)

static ts_snap_t g_snap;
static uint8_t g_src[BUF_MAX];
static uint8_t g_dst[BUF_MAX];
static uint8_t g_before[BUF_MAX];

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

/* --- CPY-01 ------------------------------------------------------------------------------- */

void test_CPY_01_copy_from_writes_at_offset(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    const size_t n = 17u;
    bloc_handle_t b;
    uint8_t *area;
    size_t off;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    b = alloc_buf(&pool, 2u * PA);
    off = bloc_headroom(b);
    TEST_ASSERT_TRUE(n <= e - off);
    area = ts_area(b);
    ts_fill(area, e, 0x40u);
    memcpy(g_before, area, e);
    ts_fill(g_src, n, 0x01u);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy_from(b, g_src, (bloc_size_t)n));
    TEST_ASSERT_EQUAL_UINT(n, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(off, bloc_headroom(b));
    TEST_ASSERT_EQUAL_PTR(area + off, bloc_data(b));
    ts_check_fill(area + off, n, 0x01u);
    ts_check_same_outside(area, g_before, e, off, off + n);
    release(b);
}

/* --- CPY-02 ------------------------------------------------------------------------------- */

void test_CPY_02_copy_from_bounds(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    bloc_handle_t b;
    size_t room;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    b = alloc_buf(&pool, PA);
    room = e - bloc_headroom(b);
    ts_fill(g_src, e + 1u, 0x10u);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy_from(b, g_src, (bloc_size_t)room));
    TEST_ASSERT_EQUAL_UINT(room, bloc_len(b));
    ts_check_fill(ts_area(b) + bloc_headroom(b), room, 0x10u);
#if BLOC_CHECKS
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 3u));
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS,
                                        (int)bloc_copy_from(b, g_src, (bloc_size_t)(room + 1u))));
    TS_SNAP_CHECK(&g_snap);
    TEST_ASSERT_EQUAL_UINT(3u, bloc_len(b));
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_copy_from(b, g_src, SIZE_MAX_V)));
    TS_SNAP_CHECK(&g_snap);
#endif
    release(b);
}

/* --- CPY-03 ------------------------------------------------------------------------------- */

void test_CPY_03_copy_from_zero_length(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = alloc_buf(&pool, PA);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 5u));
    snap_pool(&pool);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy_from(b, g_src, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_len(b));
    b->len = 5u; /* restore len, which is the only field copy_from may touch */
    TS_SNAP_CHECK(&g_snap);
    release(b);
}

/* --- CPY-04 ------------------------------------------------------------------------------- */

#if BLOC_CHECKS
void test_CPY_04_copy_from_null(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;
    size_t n;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = alloc_buf(&pool, 0u);
    snap_pool(&pool);
    for (n = 0u; n <= 4u; n += 4u) {
        TS_CHK_ASSERT(
            TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_copy_from(NULL, g_src, (bloc_size_t)n)));
        TS_CHK_ASSERT(
            TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_copy_from(b, NULL, (bloc_size_t)n)));
        TS_SNAP_CHECK(&g_snap);
    }
    release(b);
}
#endif

/* --- CPY-05 ------------------------------------------------------------------------------- */

void test_CPY_05_copy_to_table(void)
{
    bloc_pool_t pool;
    const size_t len = 12u;
    bloc_handle_t b;
    size_t i;
    const size_t ok[][2] = {{0u, len}, {3u, len - 3u}, {len, 0u}, {0u, 0u}};
    const size_t bad[][2] = {
        {len + 1u, 0u}, {2u, len - 1u}, {(size_t)SIZE_MAX_V, 1u}, {1u, (size_t)SIZE_MAX_V}};

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = alloc_buf(&pool, PA);
    ts_fill((uint8_t *)bloc_data(b), len, 0x30u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, (bloc_size_t)len));

    for (i = 0u; i < sizeof(ok) / sizeof(ok[0]); i++) {
        const size_t pos = ok[i][0];
        const size_t n = ok[i][1];

        snap_pool(&pool);
        memset(g_dst, 0xEEu, sizeof(g_dst));
        TEST_ASSERT_EQUAL_INT(BLOC_OK,
                              (int)bloc_copy_to(b, g_dst, (bloc_size_t)n, (bloc_size_t)pos));
        ts_check_fill(g_dst, n, (uint8_t)(0x30u + pos));
        TEST_ASSERT_TRUE(g_dst[n] == 0xEEu);
        TEST_ASSERT_TRUE(g_dst[n + 1u] == 0xEEu);
        TS_SNAP_CHECK(&g_snap);
    }
#if BLOC_CHECKS
    for (i = 0u; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snap_pool(&pool);
        memset(g_dst, 0xEEu, sizeof(g_dst));
        TS_CHK_ASSERT(
            TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_copy_to(b, g_dst, (bloc_size_t)bad[i][1],
                                                                 (bloc_size_t)bad[i][0])));
        TEST_ASSERT_TRUE(g_dst[0] == 0xEEu && g_dst[1] == 0xEEu);
        TS_SNAP_CHECK(&g_snap);
    }
#else
    (void)bad;
#endif
    release(b);
}

/* --- CPY-06 ------------------------------------------------------------------------------- */

#if BLOC_CHECKS
void test_CPY_06_copy_to_null(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = alloc_buf(&pool, 0u);
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_copy_to(NULL, g_dst, 0u, 0u)));
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_copy_to(b, NULL, 0u, 0u)));
    TS_SNAP_CHECK(&g_snap);
    release(b);
}
#endif

/* --- CPY-07 ------------------------------------------------------------------------------- */

void test_CPY_07_copy_between_buffers(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    const size_t n = 10u;
    bloc_handle_t src;
    bloc_handle_t dst;
    const uint8_t *sdata;
    size_t src_off;
    size_t dst_off;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    src = alloc_buf(&pool, PA);
    dst = alloc_buf(&pool, 2u * PA);
    src_off = bloc_headroom(src);
    dst_off = bloc_headroom(dst);
    sdata = (const uint8_t *)bloc_data(src);
    ts_fill(ts_area(src), e, 0x50u);
    ts_fill(ts_area(dst), e, 0xA0u);
    memcpy(g_before, ts_area(dst), e);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(src, (bloc_size_t)n));
    memcpy(g_src, ts_area(src), e);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy(dst, src));
    TEST_ASSERT_EQUAL_UINT(n, bloc_len(dst));
    TEST_ASSERT_EQUAL_UINT(dst_off, bloc_headroom(dst));
    TEST_ASSERT_EQUAL_MEMORY(sdata, bloc_data(dst), n);
    ts_check_same_outside(ts_area(dst), g_before, e, dst_off, dst_off + n);
    /* The source is unchanged. */
    TEST_ASSERT_EQUAL_UINT(n, bloc_len(src));
    TEST_ASSERT_EQUAL_UINT(src_off, bloc_headroom(src));
    TEST_ASSERT_EQUAL_MEMORY(g_src, ts_area(src), e);
    release(src);
    release(dst);
}

/* --- CPY-08 ------------------------------------------------------------------------------- */

void test_CPY_08_copy_across_pools(void)
{
    bloc_pool_t big;
    bloc_pool_t small;
    const size_t e = ts_element_size_aligned();
    const size_t es = e / 2u;
    bloc_handle_t src;
    bloc_handle_t dst;
    size_t room;

    ts_pool_setup(&big, 1u, (bloc_size_t)e);
    ts_pool_setup(&small, 1u, (bloc_size_t)es);
    src = alloc_buf(&big, 0u);
    dst = alloc_buf(&small, PA);
    room = es - bloc_headroom(dst);
    ts_fill(ts_area(src), e, 0x60u);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(src, (bloc_size_t)room));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy(dst, src));
    TEST_ASSERT_EQUAL_UINT(room, bloc_len(dst));
    TEST_ASSERT_EQUAL_MEMORY(bloc_data(src), bloc_data(dst), room);
#if BLOC_CHECKS
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(src, (bloc_size_t)(room + 1u)));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(dst, 2u));
    snap_pool(&small);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_copy(dst, src)));
    TS_SNAP_CHECK(&g_snap);
    TEST_ASSERT_EQUAL_UINT(2u, bloc_len(dst));
#endif
    release(src);
    release(dst);
}

/* --- CPY-09 ------------------------------------------------------------------------------- */

void test_CPY_09_copy_to_itself(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = alloc_buf(&pool, PA);
    ts_fill(ts_area(b), (size_t)pool.element_size, 0x70u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 9u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_retain(b));
    snap_pool(&pool);
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy(b, b)));
    TS_SNAP_CHECK(&g_snap);
    TEST_ASSERT_EQUAL_UINT(2u, b->refcount);
    release(b);
    release(b);
}

/* --- CPY-10 ------------------------------------------------------------------------------- */

void test_CPY_10_copy_empty_source(void)
{
    bloc_pool_t pool;
    bloc_handle_t src;
    bloc_handle_t dst;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    src = alloc_buf(&pool, 0u);
    dst = alloc_buf(&pool, PA);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(dst, 5u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy(dst, src));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_len(dst));
    TEST_ASSERT_EQUAL_UINT(PA, bloc_headroom(dst));
    release(src);
    release(dst);
}

/* --- CPY-11 ------------------------------------------------------------------------------- */

#if BLOC_CHECKS
void test_CPY_11_copy_null(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = alloc_buf(&pool, 0u);
    snap_pool(&pool);
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_copy(NULL, b)));
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_copy(b, NULL)));
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_copy(NULL, NULL)));
    TS_SNAP_CHECK(&g_snap);
    release(b);
}
#endif

/* --- CPY-12 ------------------------------------------------------------------------------- */

void test_CPY_12_copy_to_shared_buffer(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;
    bloc_handle_t dst;
    const size_t n = 6u;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    b = alloc_buf(&pool, PA);
    dst = alloc_buf(&pool, PA);
    ts_fill((uint8_t *)bloc_data(b), n, 0x21u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, (bloc_size_t)n));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_retain(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_retain(b));

    /* Reading a shared buffer, as the source of copy_to and of bloc_copy, is allowed. */
    TS_EXPECT_NO_ASSERT(
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy_to(b, g_dst, (bloc_size_t)n, 0u)));
    ts_check_fill(g_dst, n, 0x21u);
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy(dst, b)));
    TEST_ASSERT_EQUAL_UINT(n, bloc_len(dst));
    release(b);
    release(b);
    release(b);
    release(dst);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_CPY_01_copy_from_writes_at_offset);
    RUN_TEST(test_CPY_02_copy_from_bounds);
    RUN_TEST(test_CPY_03_copy_from_zero_length);
#if BLOC_CHECKS
    RUN_TEST(test_CPY_04_copy_from_null);
#endif
    RUN_TEST(test_CPY_05_copy_to_table);
#if BLOC_CHECKS
    RUN_TEST(test_CPY_06_copy_to_null);
#endif
    RUN_TEST(test_CPY_07_copy_between_buffers);
    RUN_TEST(test_CPY_08_copy_across_pools);
    RUN_TEST(test_CPY_09_copy_to_itself);
    RUN_TEST(test_CPY_10_copy_empty_source);
#if BLOC_CHECKS
    RUN_TEST(test_CPY_11_copy_null);
#endif
    RUN_TEST(test_CPY_12_copy_to_shared_buffer);
    return UNITY_END();
}
