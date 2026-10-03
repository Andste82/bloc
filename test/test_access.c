/*
 * Accessor tests ACC-01..ACC-04 (implementation plan, section 8.5).
 *
 * ACC-01 is completed in later phases: it is checked here after allocation and calloc, the
 * length, copy, append and prepend operations extend it when they exist.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bloc.h"
#include "ts_arena.h"
#include "ts_helpers.h"
#include "unity.h"

#define PA ((size_t)BLOC_PAYLOAD_ALIGNMENT)

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

/* Checks the four accessors against the definitions of spec section 10. */
static void check_view(const bloc_pool_t *pool, bloc_handle_t b, size_t offset, size_t len)
{
    const size_t e = (size_t)pool->element_size;

    TEST_ASSERT_EQUAL_PTR((uint8_t *)b + ts_expected_header() + offset, bloc_data(b));
    TEST_ASSERT_EQUAL_UINT(len, bloc_len(b));
    TEST_ASSERT_EQUAL_UINT(offset, bloc_headroom(b));
    TEST_ASSERT_EQUAL_UINT(e - offset - len, bloc_tailroom(b));
    TEST_ASSERT_EQUAL_UINT(e, (size_t)bloc_headroom(b) + (size_t)bloc_len(b) +
                                  (size_t)bloc_tailroom(b));
}

/* --- ACC-01 ------------------------------------------------------------------------------- */

void test_ACC_01_view_after_alloc(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    const size_t table[] = {0u, 1u, PA, PA + 1u, e - PA, e};
    size_t i;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    for (i = 0u; i < sizeof(table) / sizeof(table[0]); i++) {
        bloc_handle_t b = bloc_alloc(&pool, (bloc_size_t)table[i]);
        bloc_handle_t c = bloc_calloc(&pool, (bloc_size_t)table[i]);

        TEST_ASSERT_NOT_NULL(b);
        TEST_ASSERT_NOT_NULL(c);
        check_view(&pool, b, ts_round_up_pa(table[i]), 0u);
        check_view(&pool, c, ts_round_up_pa(table[i]), 0u);
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(c));
    }
}

/* --- ACC-02 ------------------------------------------------------------------------------- */

#if BLOC_CHECKS
void test_ACC_02_null_handle(void)
{
    static int sentinel;
    void *p = &sentinel;
    bloc_size_t n = 1u;

    TS_CHK_ASSERT(p = bloc_data(NULL));
    TEST_ASSERT_NULL(p);
    TS_CHK_ASSERT(n = bloc_len(NULL));
    TEST_ASSERT_EQUAL_UINT(0u, n);
    n = 1u;
    TS_CHK_ASSERT(n = bloc_headroom(NULL));
    TEST_ASSERT_EQUAL_UINT(0u, n);
    n = 1u;
    TS_CHK_ASSERT(n = bloc_tailroom(NULL));
    TEST_ASSERT_EQUAL_UINT(0u, n);
}
#endif

/* --- ACC-03 ------------------------------------------------------------------------------- */

void test_ACC_03_shared_buffer(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, 32u);
    b = bloc_alloc(&pool, PA);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_retain(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_retain(b));
    TEST_ASSERT_EQUAL_UINT(3u, b->refcount);

    TS_EXPECT_NO_ASSERT(check_view(&pool, b, ts_round_up_pa(PA), 0u));

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

/* --- ACC-04 ------------------------------------------------------------------------------- */

#if TS_HAVE_TRACER
void test_ACC_04_accessors_do_not_lock(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;

    ts_pool_setup(&pool, 2u, 32u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);

    TS_EXPECT_LOCKS(0, TEST_ASSERT_NOT_NULL(bloc_data(b)));
    TS_EXPECT_LOCKS(0, TEST_ASSERT_EQUAL_UINT(0u, bloc_len(b)));
    TS_EXPECT_LOCKS(0, TEST_ASSERT_EQUAL_UINT(0u, bloc_headroom(b)));
    TS_EXPECT_LOCKS(0, TEST_ASSERT_EQUAL_UINT(32u, bloc_tailroom(b)));
#if BLOC_CHECKS
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(0, TEST_ASSERT_NULL(bloc_data(NULL))));
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(0, TEST_ASSERT_EQUAL_UINT(0u, bloc_len(NULL))));
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(0, TEST_ASSERT_EQUAL_UINT(0u, bloc_headroom(NULL))));
    TS_CHK_ASSERT(TS_EXPECT_LOCKS(0, TEST_ASSERT_EQUAL_UINT(0u, bloc_tailroom(NULL))));
#endif
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}
#endif

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ACC_01_view_after_alloc);
#if BLOC_CHECKS
    RUN_TEST(test_ACC_02_null_handle);
#endif
    RUN_TEST(test_ACC_03_shared_buffer);
#if TS_HAVE_TRACER
    RUN_TEST(test_ACC_04_accessors_do_not_lock);
#endif
    return UNITY_END();
}
