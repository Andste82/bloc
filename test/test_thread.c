/*
 * Thread-safety tests (tag TS, implementation plan, section 8.11). Only the TS-01 rows for the
 * functions of the complete API are here, followed by TS-02 (no nesting) and TS-03 (bloc_calloc
 * zeroes outside the lock). TS-04, the pthread stress test, is in test_stress_pthread.c.
 * The suite runs only where the lock tracer is active (BLOC_THREAD_SAFE with the ts_lock macros).
 *
 * TS-01: protected functions enter the lock exactly once per call on every path after their
 * parameter checks, unprotected functions never. The lock balance and the absence of nesting are
 * checked for every test by tearDown().
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bloc.h"
#include "ts_arena.h"
#include "ts_helpers.h"
#include "unity.h"

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

#if TS_HAVE_TRACER

/* See test_copy.c: a volatile value keeps link-time optimization from folding the bounds checks. */
static volatile bloc_size_t g_size_max = BLOC_SIZE_MAX;
#define SIZE_MAX_V ((bloc_size_t)g_size_max)

/* One protected function, one path, and how often it must enter the lock. */
static void check_row(const char *name, unsigned expected, unsigned actual)
{
    TEST_ASSERT_EQUAL_UINT_MESSAGE(expected, actual, name);
}

#define ROW(name, n, stmt)                                                                         \
    do {                                                                                           \
        const unsigned before_ = ts_lock_enters;                                                   \
        stmt;                                                                                      \
        check_row(name, (unsigned)(n), ts_lock_enters - before_);                                  \
    } while (0)

void test_TS_01_pool_functions(void)
{
    bloc_pool_t pool;
    static bloc_pool_t zeroed;
    const size_t size = BLOC_POOL_SIZE(2, 16);
    uint8_t *st = ts_storage(size, 0u);
    bloc_handle_t b = NULL;
    bloc_status_t r = BLOC_OK;
    bloc_count_t n = 0u;

    ROW("pool_init ok", 0, r = bloc_pool_init(&pool, st, size, 2u, 16u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
    ROW("pool_init invalid", 0, TS_CHK_ASSERT(r = bloc_pool_init(&pool, st, size, 0u, 16u)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);

    ROW("free_count", 1, n = bloc_pool_free_count(&pool));
    TEST_ASSERT_EQUAL_UINT(2u, n);

    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);
    ROW("deinit busy", 1, r = bloc_pool_deinit(&pool));
    TEST_ASSERT_EQUAL_INT(BLOC_BUSY, (int)r);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    ROW("deinit ok", 1, r = bloc_pool_deinit(&pool));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
    ROW("deinit uninitialized", 1, TS_CHK_ASSERT(r = bloc_pool_deinit(&pool)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);

    memset(&zeroed, 0, sizeof(zeroed));
    ROW("free_count uninitialized", 1, n = bloc_pool_free_count(&zeroed));
    TEST_ASSERT_EQUAL_UINT(0u, n);
#if BLOC_CHECKS
    ROW("deinit NULL", 0, TS_CHK_ASSERT(r = bloc_pool_deinit(NULL)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    ROW("free_count NULL", 0, TS_CHK_ASSERT(n = bloc_pool_free_count(NULL)));
    TEST_ASSERT_EQUAL_UINT(0u, n);
#endif
}

#if BLOC_STATS
void test_TS_01_get_stats(void)
{
    bloc_pool_t pool;
    static bloc_pool_t zeroed;
    bloc_pool_stats_t out;
    bloc_status_t r = BLOC_OK;

    ts_pool_setup(&pool, 2u, 16u);
    memset(&zeroed, 0, sizeof(zeroed));

    ROW("get_stats ok", 1, r = bloc_pool_get_stats(&pool, &out));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
    ROW("get_stats uninitialized", 1, TS_CHK_ASSERT(r = bloc_pool_get_stats(&zeroed, &out)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    ROW("get_stats NULL pool", 0, TS_CHK_ASSERT(r = bloc_pool_get_stats(NULL, &out)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    ROW("get_stats NULL out", 0, TS_CHK_ASSERT(r = bloc_pool_get_stats(&pool, NULL)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
}
#endif

void test_TS_01_alloc_functions(void)
{
    bloc_pool_t pool;
    static bloc_pool_t zeroed;
    bloc_handle_t a = NULL;
    bloc_handle_t b = NULL;
    bloc_handle_t c = &(struct bloc_handle){0};

    ts_pool_setup(&pool, 2u, 16u);
    memset(&zeroed, 0, sizeof(zeroed));

    ROW("alloc ok", 1, a = bloc_alloc(&pool, 0u));
    ROW("calloc ok", 1, b = bloc_calloc(&pool, 0u));
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    ROW("alloc empty", 1, c = bloc_alloc(&pool, 0u));
    TEST_ASSERT_NULL(c);
    ROW("calloc empty", 1, c = bloc_calloc(&pool, 0u));
    TEST_ASSERT_NULL(c);
    ROW("alloc oversize headroom", 0, c = bloc_alloc(&pool, SIZE_MAX_V));
    TEST_ASSERT_NULL(c);
    ROW("calloc oversize headroom", 0, c = bloc_calloc(&pool, SIZE_MAX_V));
    TEST_ASSERT_NULL(c);
#if BLOC_CHECKS
    ROW("alloc NULL pool", 0, TS_CHK_ASSERT(c = bloc_alloc(NULL, 0u)));
    TEST_ASSERT_NULL(c);
    c = a;
    ROW("alloc uninitialized", 0, TS_CHK_ASSERT(c = bloc_alloc(&zeroed, 0u)));
    TEST_ASSERT_NULL(c);
    c = a;
    ROW("calloc NULL pool", 0, TS_CHK_ASSERT(c = bloc_calloc(NULL, 0u)));
    TEST_ASSERT_NULL(c);
#endif
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(a));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

void test_TS_01_retain_release(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;
    bloc_handle_t free_block;
    bloc_status_t r = BLOC_OK;

    ts_pool_setup(&pool, 3u, 16u);
    b = bloc_alloc(&pool, 0u);
    free_block = ts_block(&pool, 2u);
    TEST_ASSERT_NOT_NULL(b);

    ROW("retain ok", 1, r = bloc_retain(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
    ROW("release shared", 1, r = bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
    b->refcount = BLOC_REFCOUNT_MAX;
    ROW("retain overflow", 1, r = bloc_retain(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OVERFLOW, (int)r);
    b->refcount = 1u;
    ROW("retain free block", 1, TS_CHK_ASSERT(r = bloc_retain(free_block)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    ROW("release free block", 1, TS_CHK_ASSERT(r = bloc_release(free_block)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    ROW("release final", 1, r = bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
    ROW("release NULL", 0, r = bloc_release(NULL));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
#if BLOC_CHECKS
    ROW("retain NULL", 0, TS_CHK_ASSERT(r = bloc_retain(NULL)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
#endif
}

#if BLOC_DEBUG
void test_TS_01_invalid_handles_lock_once(void)
{
    bloc_pool_t pool;
    struct bloc_handle fake;
    bloc_status_t r = BLOC_OK;

    ts_pool_setup(&pool, 2u, 16u);
    memset(&fake, 0, sizeof(fake));
    fake.refcount = 1u;
    fake.link.pool = &pool;

    ROW("retain invalid handle", 1, TS_EXPECT_ASSERT(r = bloc_retain(&fake)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
    ROW("release invalid handle", 1, TS_EXPECT_ASSERT(r = bloc_release(&fake)));
    TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
}
#endif

void test_TS_01_accessors(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, 16u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);

    ROW("data", 0, TEST_ASSERT_NOT_NULL(bloc_data(b)));
    ROW("len", 0, TEST_ASSERT_EQUAL_UINT(0u, bloc_len(b)));
    ROW("headroom", 0, TEST_ASSERT_EQUAL_UINT(0u, bloc_headroom(b)));
    ROW("tailroom", 0, TEST_ASSERT_EQUAL_UINT(16u, bloc_tailroom(b)));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

void test_TS_01_length_operations(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, 32u);
    b = bloc_alloc(&pool, BLOC_PAYLOAD_ALIGNMENT);
    TEST_ASSERT_NOT_NULL(b);

    ROW("set_len", 0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 8u)));
    ROW("remove_header", 0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_remove_header(b, 1u)));
    ROW("add_header", 0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_add_header(b, 1u)));
#if BLOC_CHECKS
    ROW("set_len bounds", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_set_len(b, SIZE_MAX_V))));
    ROW("remove_header bounds", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_remove_header(b, SIZE_MAX_V))));
    ROW("add_header bounds", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_add_header(b, SIZE_MAX_V))));
    ROW("set_len NULL", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_set_len(NULL, 0u))));
    ROW("remove_header NULL", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_remove_header(NULL, 0u))));
    ROW("add_header NULL", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_add_header(NULL, 0u))));
#endif
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

void test_TS_01_copy_append_prepend(void)
{
    bloc_pool_t pool;
    bloc_handle_t a;
    bloc_handle_t b;
    uint8_t buf[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    a = bloc_alloc(&pool, 4u * BLOC_PAYLOAD_ALIGNMENT);
    b = bloc_alloc(&pool, 4u * BLOC_PAYLOAD_ALIGNMENT);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    ROW("copy_from", 0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy_from(a, buf, 8u)));
    ROW("copy_to", 0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy_to(a, buf, 8u, 0u)));
    ROW("copy", 0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy(b, a)));
    ROW("copy self", 0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy(b, b)));
    ROW("append", 0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append(b, a, 2u)));
    ROW("append_data", 0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append_data(b, buf, 2u)));
    ROW("prepend", 0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend(b, a, 2u)));
    ROW("prepend_data", 0, TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend_data(b, buf, 2u)));
#if BLOC_CHECKS
    ROW("copy_from bounds", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_copy_from(a, buf, SIZE_MAX_V))));
    ROW("copy_to bounds", 0,
        TS_CHK_ASSERT(
            TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_copy_to(a, buf, SIZE_MAX_V, 0u))));
    ROW("copy NULL", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_copy(NULL, a))));
    ROW("append bounds", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_append(b, a, SIZE_MAX_V))));
    ROW("append_data bounds", 0,
        TS_CHK_ASSERT(
            TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_append_data(b, buf, SIZE_MAX_V))));
    ROW("prepend bounds", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_prepend(b, a, SIZE_MAX_V))));
    ROW("prepend_data bounds", 0,
        TS_CHK_ASSERT(
            TEST_ASSERT_EQUAL_INT(BLOC_BOUNDS, (int)bloc_prepend_data(b, buf, SIZE_MAX_V))));
    ROW("copy_from NULL", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_copy_from(NULL, buf, 0u))));
    ROW("copy_to NULL", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_copy_to(NULL, buf, 0u, 0u))));
    ROW("append NULL", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_append(NULL, a, 0u))));
    ROW("prepend NULL", 0,
        TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_prepend(b, NULL, 0u))));
#endif
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(a));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

/* --- TS-02 -------------------------------------------------------------------------------- */

/* The depth observed inside the unlock of every lock section. */
static int g_hook_depth_max;
static unsigned g_hook_calls;

static void depth_hook(void)
{
    g_hook_calls++;
    if (ts_lock_depth > g_hook_depth_max) {
        g_hook_depth_max = ts_lock_depth;
    }
}

/*
 * A workload that uses every public function on its normal and its failure paths, including the
 * paths that assert in debug builds. The lock is never entered while it is held: the depth
 * peaks at 1, no nesting error is recorded, and every section is left again.
 */
void test_TS_02_no_nesting(void)
{
    bloc_pool_t pool;
    static bloc_pool_t zeroed;
    bloc_handle_t a;
    bloc_handle_t b;
    bloc_handle_t c;
    uint8_t buf[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    unsigned n;

    memset(&zeroed, 0, sizeof(zeroed));
    g_hook_depth_max = 0;
    g_hook_calls = 0u;
    ts_lock_exit_hook = depth_hook;

    ts_pool_setup(&pool, 2u, (bloc_size_t)ts_element_size_aligned());
    a = bloc_alloc(&pool, 4u * BLOC_PAYLOAD_ALIGNMENT);
    b = bloc_calloc(&pool, 4u * BLOC_PAYLOAD_ALIGNMENT);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    c = bloc_alloc(&pool, 0u); /* empty pool */
    TEST_ASSERT_NULL(c);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy_from(a, buf, 8u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy(b, a));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append(b, a, 2u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend_data(b, buf, 2u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_retain(a));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(a));
    TEST_ASSERT_EQUAL_UINT(0u, bloc_pool_free_count(&pool));
    TEST_ASSERT_EQUAL_INT(BLOC_BUSY, (int)bloc_pool_deinit(&pool));
#if BLOC_STATS
    {
        bloc_pool_stats_t stats;

        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_get_stats(&pool, &stats));
        TS_CHK_ASSERT(
            TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_pool_get_stats(&zeroed, &stats)));
    }
#endif
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_pool_deinit(&zeroed)));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(a));
    TS_CHK_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)bloc_release(a))); /* already free */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_deinit(&pool));

    n = g_hook_calls;
    ts_lock_exit_hook = NULL;
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_EQUAL_INT(1, g_hook_depth_max);
    TEST_ASSERT_EQUAL_INT(1, ts_lock_max_depth);
    TEST_ASSERT_EQUAL_UINT(ts_lock_enters, n);
    TEST_ASSERT_EQUAL_UINT(0u, ts_lock_nesting_errors);
    TEST_ASSERT_EQUAL_INT(0, ts_lock_depth);
}

/* --- TS-03 -------------------------------------------------------------------------------- */

static const uint8_t *g_probe_area;
static size_t g_probe_size;
static unsigned g_probe_calls;
static int g_probe_dirty;

/* Called inside the unlock: is the data area of the block still dirty at that moment? */
static void probe_hook(void)
{
    size_t i;

    g_probe_calls++;
    g_probe_dirty = 0;
    for (i = 0u; i < g_probe_size; i++) {
        if (g_probe_area[i] != 0u) {
            g_probe_dirty = 1;
        }
    }
}

static void check_area(const uint8_t *area, size_t size, uint8_t value)
{
    size_t i;

    for (i = 0u; i < size; i++) {
        TEST_ASSERT_EQUAL_HEX8(value, area[i]);
    }
}

void test_TS_03_calloc_zeroes_outside_the_lock(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    struct bloc_handle *target;
    bloc_handle_t b;
    bloc_size_t headroom;

    for (headroom = 0u; headroom <= 2u * BLOC_PAYLOAD_ALIGNMENT;
         headroom += BLOC_PAYLOAD_ALIGNMENT) {
        ts_pool_setup(&pool, 2u, (bloc_size_t)e);
        target = pool.free_head; /* the block that the next allocation takes */
        TEST_ASSERT_NOT_NULL(target);
        memset(ts_area(target), 0xFF, e);

        /* bloc_calloc: dirty while the lock is released, zero when the call returns */
        g_probe_area = ts_area(target);
        g_probe_size = e;
        g_probe_calls = 0u;
        g_probe_dirty = 0;
        ts_lock_exit_hook = probe_hook;
        b = bloc_calloc(&pool, headroom);
        ts_lock_exit_hook = NULL;
        TEST_ASSERT_EQUAL_PTR(target, b);
        TEST_ASSERT_EQUAL_UINT(1u, g_probe_calls);
        TEST_ASSERT_EQUAL_INT(1, g_probe_dirty);
        check_area(ts_area(b), e, 0x00u);
        TEST_ASSERT_EQUAL_UINT(0u, b->len);
        TEST_ASSERT_EQUAL_UINT(headroom, b->offset);
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));

        /* control: bloc_alloc leaves the area alone */
        memset(ts_area(target), 0xFF, e);
        b = bloc_alloc(&pool, headroom);
        TEST_ASSERT_EQUAL_PTR(target, b);
        check_area(ts_area(b), e, 0xFFu);
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_deinit(&pool));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_TS_01_pool_functions);
#if BLOC_STATS
    RUN_TEST(test_TS_01_get_stats);
#endif
    RUN_TEST(test_TS_01_alloc_functions);
    RUN_TEST(test_TS_01_retain_release);
#if BLOC_DEBUG
    RUN_TEST(test_TS_01_invalid_handles_lock_once);
#endif
    RUN_TEST(test_TS_01_accessors);
    RUN_TEST(test_TS_01_length_operations);
    RUN_TEST(test_TS_01_copy_append_prepend);
    RUN_TEST(test_TS_02_no_nesting);
    RUN_TEST(test_TS_03_calloc_zeroes_outside_the_lock);
    return UNITY_END();
}

#else /* no lock tracer */

int main(void)
{
    UNITY_BEGIN();
    return UNITY_END();
}

#endif
