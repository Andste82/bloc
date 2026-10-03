/*
 * Accessor tests ACC-01..ACC-04 (implementation plan, section 8.5).
 *
 * ACC-01 is checked here after allocation and calloc and after each length operation, and after
 * copy_from, append_data and prepend_data.
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

void test_ACC_01_view_after_length_operations(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    bloc_handle_t b;
    size_t off;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    b = bloc_alloc(&pool, (bloc_size_t)(2u * PA));
    TEST_ASSERT_NOT_NULL(b);
    off = ts_round_up_pa(2u * PA);
    check_view(&pool, b, off, 0u);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 10u));
    check_view(&pool, b, off, 10u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, (bloc_size_t)(e - off)));
    check_view(&pool, b, off, e - off);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 8u));
    check_view(&pool, b, off, 8u);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_add_header(b, (bloc_size_t)PA));
    check_view(&pool, b, off - PA, 8u + PA);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_add_header(b, (bloc_size_t)(off - PA)));
    check_view(&pool, b, 0u, 8u + off);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_remove_header(b, 5u));
    check_view(&pool, b, 5u, 3u + off);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_remove_header(b, (bloc_size_t)(3u + off)));
    check_view(&pool, b, 5u + 3u + off, 0u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

void test_ACC_01_view_after_copy_operations(void)
{
    bloc_pool_t pool;
    const size_t e = ts_element_size_aligned();
    uint8_t src[16];
    bloc_handle_t b;
    size_t off;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    ts_fill(src, sizeof(src), 0x01u);
    b = bloc_alloc(&pool, (bloc_size_t)(4u * PA));
    TEST_ASSERT_NOT_NULL(b);
    off = ts_round_up_pa(4u * PA);

    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy_from(b, src, 6u));
    check_view(&pool, b, off, 6u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append_data(b, src, 4u));
    check_view(&pool, b, off, 10u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend_data(b, src, 3u));
    check_view(&pool, b, off - 3u, 13u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy_from(b, src, 2u));
    check_view(&pool, b, off - 3u, 2u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
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
    RUN_TEST(test_ACC_01_view_after_length_operations);
    RUN_TEST(test_ACC_01_view_after_copy_operations);
#if BLOC_CHECKS
    RUN_TEST(test_ACC_02_null_handle);
#endif
    RUN_TEST(test_ACC_03_shared_buffer);
#if TS_HAVE_TRACER
    RUN_TEST(test_ACC_04_accessors_do_not_lock);
#endif
    return UNITY_END();
}
