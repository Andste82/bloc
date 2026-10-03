/*
 * Thread-safety tests (tag TS, implementation plan, section 8.11). Only the TS-01 rows for the
 * functions that exist are here; TS-02..TS-04 follow in phase 5. The suite runs only where the
 * lock tracer is active (BLOC_THREAD_SAFE with the ts_lock macros).
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
    ROW("alloc oversize headroom", 0, c = bloc_alloc(&pool, BLOC_SIZE_MAX));
    TEST_ASSERT_NULL(c);
    ROW("calloc oversize headroom", 0, c = bloc_calloc(&pool, BLOC_SIZE_MAX));
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
    ROW("set_len bounds", 0, TS_CHK_ASSERT((void)bloc_set_len(b, BLOC_SIZE_MAX)));
    ROW("remove_header bounds", 0, TS_CHK_ASSERT((void)bloc_remove_header(b, BLOC_SIZE_MAX)));
    ROW("add_header bounds", 0, TS_CHK_ASSERT((void)bloc_add_header(b, BLOC_SIZE_MAX)));
    ROW("set_len NULL", 0, TS_CHK_ASSERT((void)bloc_set_len(NULL, 0u)));
    ROW("remove_header NULL", 0, TS_CHK_ASSERT((void)bloc_remove_header(NULL, 0u)));
    ROW("add_header NULL", 0, TS_CHK_ASSERT((void)bloc_add_header(NULL, 0u)));
#endif
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
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
    return UNITY_END();
}

#else /* no lock tracer */

int main(void)
{
    UNITY_BEGIN();
    return UNITY_END();
}

#endif
