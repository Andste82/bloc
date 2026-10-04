/*
 * Compile-time configuration and layout tests CFG-01..CFG-11 (implementation plan, 8.1).
 *
 * Expected values come from sizeof, _Alignof and the formulas of the specification (through the
 * helpers of ts_helpers.h), never from literals (Q-06). The compile-time parts are repeated as
 * _Static_asserts in test/target/layout_static.c, which is also compiled into this executable.
 */
#include <stddef.h>
#include <stdint.h>

#include "bloc/bloc.h"
#include "ts_arena.h"
#include "ts_helpers.h"
#include "unity.h"

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

/* Rounds x up to a multiple of a in the widest integer type, by division instead of masking. */
static uintmax_t wide_round_up(uintmax_t x, uintmax_t a) { return (x + a - 1u) / a * a; }

/* --- CFG-01 ------------------------------------------------------------------------------ */

/* BLOC_POOL_SIZE sizes a file-scope array and appears in a _Static_assert. */
static uint8_t g_cfg01_array[BLOC_POOL_SIZE(5, 20)];
_Static_assert(BLOC_POOL_SIZE(5, 20) > 0u, "BLOC_POOL_SIZE must be an integer constant expression");
_Static_assert(BLOC_BLOCK_STRIDE(20) > 20u, "BLOC_BLOCK_STRIDE must be an integer constant");
_Static_assert(BLOC_HEADER_SIZE > 0u, "BLOC_HEADER_SIZE must be an integer constant");
_Static_assert(BLOC_STORAGE_ALIGNMENT > 0u, "BLOC_STORAGE_ALIGNMENT must be an integer constant");
_Static_assert(BLOC_ELEMENT_SIZE_MAX > 0u, "BLOC_ELEMENT_SIZE_MAX must be an integer constant");

void test_CFG_01_pool_size_is_constant_expression(void)
{
    TEST_ASSERT_EQUAL_UINT(5u * ts_expected_stride(20u), sizeof(g_cfg01_array));
    TEST_ASSERT_EQUAL_UINT(BLOC_POOL_SIZE(5, 20), sizeof(g_cfg01_array));
    /* The last byte of the array is addressable. */
    g_cfg01_array[sizeof(g_cfg01_array) - 1u] = 0x42u;
    TEST_ASSERT_EQUAL_HEX8(0x42u, g_cfg01_array[BLOC_POOL_SIZE(5, 20) - 1u]);
}

/* --- CFG-02 ------------------------------------------------------------------------------ */

void test_CFG_02_pool_size_formula(void)
{
    static const size_t counts[] = {1u, 2u, 7u};
    const size_t pa = (size_t)BLOC_PAYLOAD_ALIGNMENT;
    const size_t sa = (size_t)BLOC_STORAGE_ALIGNMENT;
    size_t elements[8];
    size_t n_elements = 0u;
    size_t i;
    size_t k;

    elements[n_elements++] = 1u;
    if (pa - 1u > 0u) {
        elements[n_elements++] = pa - 1u;
    }
    elements[n_elements++] = pa;
    elements[n_elements++] = pa + 1u;
    if (sa - 1u > 0u) {
        elements[n_elements++] = sa - 1u;
    }
    elements[n_elements++] = sa;
    elements[n_elements++] = sa + 1u;
    elements[n_elements++] = 255u;

    for (i = 0u; i < sizeof(counts) / sizeof(counts[0]); i++) {
        for (k = 0u; k < n_elements; k++) {
            const size_t e = elements[k];
            TEST_ASSERT_EQUAL_UINT(ts_expected_stride(e), BLOC_BLOCK_STRIDE(e));
            TEST_ASSERT_EQUAL_UINT(counts[i] * ts_expected_stride(e), BLOC_POOL_SIZE(counts[i], e));
            TEST_ASSERT_TRUE(ts_expected_header() + e <= ts_expected_stride(e));
            TEST_ASSERT_EQUAL_UINT(0u, BLOC_BLOCK_STRIDE(e) % sa);
        }
    }
}

/* --- CFG-03 ------------------------------------------------------------------------------ */

void test_CFG_03_storage_alignment(void)
{
    const size_t sa = (size_t)BLOC_STORAGE_ALIGNMENT;

    TEST_ASSERT_EQUAL_UINT(ts_expected_storage_alignment(), sa);
    TEST_ASSERT_TRUE(sa >= (size_t)BLOC_BLOCK_ALIGNMENT);
    TEST_ASSERT_TRUE(sa >= (size_t)BLOC_PAYLOAD_ALIGNMENT);
    TEST_ASSERT_TRUE(sa >= _Alignof(struct bloc_handle));
    TEST_ASSERT_TRUE(sa == (size_t)BLOC_BLOCK_ALIGNMENT || sa == (size_t)BLOC_PAYLOAD_ALIGNMENT ||
                     sa == _Alignof(struct bloc_handle));
    TEST_ASSERT_TRUE(sa != 0u);
    TEST_ASSERT_EQUAL_UINT(0u, sa & (sa - 1u));
}

/* --- CFG-04 ------------------------------------------------------------------------------ */

void test_CFG_04_header_size(void)
{
    TEST_ASSERT_EQUAL_UINT(ts_expected_header(), BLOC_HEADER_SIZE);
    TEST_ASSERT_TRUE(BLOC_HEADER_SIZE >= sizeof(struct bloc_handle));
    TEST_ASSERT_EQUAL_UINT(0u, BLOC_HEADER_SIZE % (size_t)BLOC_PAYLOAD_ALIGNMENT);
    /* The header is padded by less than one payload alignment unit. */
    TEST_ASSERT_TRUE(BLOC_HEADER_SIZE - sizeof(struct bloc_handle) <
                     (size_t)BLOC_PAYLOAD_ALIGNMENT);
}

/* --- CFG-05 ------------------------------------------------------------------------------ */

void test_CFG_05_handle_field_order(void)
{
    TEST_ASSERT_EQUAL_UINT(0u, offsetof(struct bloc_handle, link));
    TEST_ASSERT_TRUE(offsetof(struct bloc_handle, refcount) >= sizeof(void *));
    TEST_ASSERT_TRUE(offsetof(struct bloc_handle, offset) > offsetof(struct bloc_handle, link));
    TEST_ASSERT_TRUE(offsetof(struct bloc_handle, len) > offsetof(struct bloc_handle, offset));
    TEST_ASSERT_TRUE(offsetof(struct bloc_handle, refcount) > offsetof(struct bloc_handle, len));
    /* The union holds both pointers; refcount lies behind all of it. */
    TEST_ASSERT_TRUE(sizeof(((struct bloc_handle *)0)->link) >= sizeof(void *));
}

/* --- CFG-06 ------------------------------------------------------------------------------ */

void test_CFG_06_type_maxima(void)
{
    const bloc_size_t size_max = (bloc_size_t)-1;
    const bloc_count_t count_max = (bloc_count_t)-1;
    const bloc_refcount_t refcount_max = (bloc_refcount_t)-1;

    TEST_ASSERT_TRUE(BLOC_SIZE_MAX == size_max);
    TEST_ASSERT_TRUE(BLOC_COUNT_MAX == count_max);
    TEST_ASSERT_TRUE(BLOC_REFCOUNT_MAX == refcount_max);
    TEST_ASSERT_TRUE(BLOC_SIZE_MAX > 0);
    TEST_ASSERT_TRUE(BLOC_COUNT_MAX > 0);
    TEST_ASSERT_TRUE(BLOC_REFCOUNT_MAX > 0);
    /* Adding one wraps to zero: the maxima are really the largest values of the types. */
    TEST_ASSERT_TRUE((bloc_size_t)(size_max + 1u) == 0u);
    TEST_ASSERT_TRUE((bloc_count_t)(count_max + 1u) == 0u);
    TEST_ASSERT_TRUE((bloc_refcount_t)(refcount_max + 1u) == 0u);
}

/* --- CFG-07 ------------------------------------------------------------------------------ */

void test_CFG_07_element_size_max(void)
{
    const uintmax_t sa = (uintmax_t)ts_expected_storage_alignment();
    const uintmax_t header = (uintmax_t)ts_expected_header();
    const uintmax_t e = (uintmax_t)BLOC_ELEMENT_SIZE_MAX;
    const uintmax_t stride_e = wide_round_up(header + e, sa);
    const uintmax_t stride_e1 = wide_round_up(header + e + 1u, sa);

    TEST_ASSERT_TRUE(stride_e <= (uintmax_t)BLOC_SIZE_MAX);
    TEST_ASSERT_TRUE(stride_e1 > (uintmax_t)BLOC_SIZE_MAX);
    /* The macro itself agrees with the independent computation. */
    TEST_ASSERT_TRUE((uintmax_t)BLOC_BLOCK_STRIDE(BLOC_ELEMENT_SIZE_MAX) == stride_e);
}

/* --- CFG-08 ------------------------------------------------------------------------------ */

#if !defined(BLOC_CONFIG_HEADER)
/* DEF: the library defaults of specification section 6. */
#if BLOC_BLOCK_ALIGNMENT != 4
#error "default BLOC_BLOCK_ALIGNMENT must be 4"
#endif
#if BLOC_PAYLOAD_ALIGNMENT != 4
#error "default BLOC_PAYLOAD_ALIGNMENT must be 4"
#endif
#if BLOC_THREAD_SAFE != 0
#error "default BLOC_THREAD_SAFE must be 0"
#endif
#if BLOC_CHECKS != 1
#error "default BLOC_CHECKS must be 1"
#endif
#if BLOC_DEBUG != 0
#error "default BLOC_DEBUG must be 0"
#endif
#if BLOC_STATS != 0
#error "default BLOC_STATS must be 0"
#endif

void test_CFG_08_defaults(void)
{
    TEST_ASSERT_EQUAL_UINT(sizeof(uint16_t), sizeof(bloc_size_t));
    TEST_ASSERT_EQUAL_UINT(sizeof(uint8_t), sizeof(bloc_count_t));
    TEST_ASSERT_EQUAL_UINT(sizeof(uint8_t), sizeof(bloc_refcount_t));
    TEST_ASSERT_EQUAL_UINT(1u, sizeof(bloc_count_t));
    TEST_ASSERT_EQUAL_UINT(1u, sizeof(bloc_refcount_t));
    TEST_ASSERT_EQUAL_UINT(2u, sizeof(bloc_size_t));
    TEST_ASSERT_EQUAL_UINT(0xFFFFu, BLOC_SIZE_MAX);
    TEST_ASSERT_EQUAL_UINT(0xFFu, BLOC_COUNT_MAX);
    TEST_ASSERT_EQUAL_UINT(0xFFu, BLOC_REFCOUNT_MAX);
    TEST_ASSERT_EQUAL_UINT(4u, (size_t)BLOC_BLOCK_ALIGNMENT);
    TEST_ASSERT_EQUAL_UINT(4u, (size_t)BLOC_PAYLOAD_ALIGNMENT);
    /* The default assertion handler is the trapping one; it is not compiled in a test. */
#if defined(__GNUC__) || defined(__clang__)
#ifndef BLOC_PLATFORM_ASSERT
#error "BLOC_PLATFORM_ASSERT must have a default"
#endif
#endif
    /* On a 32-bit target the header is 12 bytes (spec section 5); on 64-bit it is 16. */
    if (sizeof(void *) == 4u) {
        TEST_ASSERT_EQUAL_UINT(12u, sizeof(struct bloc_handle));
    } else if (sizeof(void *) == 8u) {
        TEST_ASSERT_EQUAL_UINT(16u, sizeof(struct bloc_handle));
    }
}
#endif

/* --- CFG-09 ------------------------------------------------------------------------------ */

static BLOC_POOL_STORAGE(g_cfg09_storage, 3, 10);

void test_CFG_09_pool_storage(void)
{
    TEST_ASSERT_EQUAL_UINT(3u * ts_expected_stride(10u), sizeof(g_cfg09_storage));
    TEST_ASSERT_EQUAL_UINT(0u,
                           (size_t)((uintptr_t)g_cfg09_storage % ts_expected_storage_alignment()));
    TEST_ASSERT_EQUAL_UINT(0u, (size_t)((uintptr_t)g_cfg09_storage % BLOC_STORAGE_ALIGNMENT));
}

/* --- CFG-10 ------------------------------------------------------------------------------ */

/*
 * Compile test: a bloc_const_handle_t is accepted by every read-only function. The calls sit in
 * sizeof() operands, which are type-checked but never evaluated, so nothing needs to be linked.
 * The _Generic checks pin the exact parameter types.
 */
#define CFG10_IS(fn, type) _Generic(&(fn), type: 1, default: 0)

_Static_assert(CFG10_IS(bloc_data, void *(*)(bloc_const_handle_t)), "bloc_data signature");
_Static_assert(CFG10_IS(bloc_len, bloc_size_t (*)(bloc_const_handle_t)), "bloc_len signature");
_Static_assert(CFG10_IS(bloc_headroom, bloc_size_t (*)(bloc_const_handle_t)),
               "bloc_headroom signature");
_Static_assert(CFG10_IS(bloc_tailroom, bloc_size_t (*)(bloc_const_handle_t)),
               "bloc_tailroom signature");
_Static_assert(CFG10_IS(bloc_copy_to,
                        bloc_status_t (*)(bloc_const_handle_t, void *, bloc_size_t, bloc_size_t)),
               "bloc_copy_to signature");
_Static_assert(CFG10_IS(bloc_copy, bloc_status_t (*)(bloc_handle_t, bloc_const_handle_t)),
               "bloc_copy signature");
_Static_assert(CFG10_IS(bloc_append,
                        bloc_status_t (*)(bloc_handle_t, bloc_const_handle_t, bloc_size_t)),
               "bloc_append signature");
_Static_assert(CFG10_IS(bloc_prepend,
                        bloc_status_t (*)(bloc_handle_t, bloc_const_handle_t, bloc_size_t)),
               "bloc_prepend signature");
_Static_assert(CFG10_IS(bloc_pool_free_count, bloc_count_t (*)(const bloc_pool_t *)),
               "bloc_pool_free_count signature");
#if BLOC_STATS
_Static_assert(CFG10_IS(bloc_pool_get_stats,
                        bloc_status_t (*)(const bloc_pool_t *, bloc_pool_stats_t *)),
               "bloc_pool_get_stats signature");
#endif

void test_CFG_10_const_handle_is_accepted(void)
{
    static struct bloc_handle storage_handle;
    static bloc_pool_t pool;
    const struct bloc_handle *ch = &storage_handle;
    bloc_const_handle_t src = ch;
    bloc_handle_t dst = &storage_handle;
    uint8_t buffer[4];

    (void)sizeof(bloc_data(src));
    (void)sizeof(bloc_len(src));
    (void)sizeof(bloc_headroom(src));
    (void)sizeof(bloc_tailroom(src));
    (void)sizeof(bloc_copy_to(src, buffer, 0u, 0u));
    (void)sizeof(bloc_copy(dst, src));
    (void)sizeof(bloc_append(dst, src, 0u));
    (void)sizeof(bloc_prepend(dst, src, 0u));
    (void)sizeof(bloc_pool_free_count(&pool));
    /* A mutable handle converts implicitly to a const one. */
    (void)sizeof(bloc_len(dst));
    TEST_PASS();
}

/* --- CFG-11 ------------------------------------------------------------------------------ */

#ifdef BLOC_EMPTY
#error "BLOC_EMPTY must not be defined"
#endif

void test_CFG_11_status_values(void)
{
    const bloc_status_t expected_order[] = {BLOC_OK,   BLOC_INVALID,  BLOC_BOUNDS,
                                            BLOC_BUSY, BLOC_OVERFLOW, BLOC_ALIGNMENT};
    size_t i;

    TEST_ASSERT_EQUAL_INT(0, (int)BLOC_OK);
    for (i = 0u; i < sizeof(expected_order) / sizeof(expected_order[0]); i++) {
        TEST_ASSERT_EQUAL_INT((int)i, (int)expected_order[i]);
    }
    TEST_ASSERT_EQUAL_INT(1, (int)BLOC_INVALID);
    TEST_ASSERT_EQUAL_INT(2, (int)BLOC_BOUNDS);
    TEST_ASSERT_EQUAL_INT(3, (int)BLOC_BUSY);
    TEST_ASSERT_EQUAL_INT(4, (int)BLOC_OVERFLOW);
    TEST_ASSERT_EQUAL_INT(5, (int)BLOC_ALIGNMENT);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_CFG_01_pool_size_is_constant_expression);
    RUN_TEST(test_CFG_02_pool_size_formula);
    RUN_TEST(test_CFG_03_storage_alignment);
    RUN_TEST(test_CFG_04_header_size);
    RUN_TEST(test_CFG_05_handle_field_order);
    RUN_TEST(test_CFG_06_type_maxima);
    RUN_TEST(test_CFG_07_element_size_max);
#if !defined(BLOC_CONFIG_HEADER)
    RUN_TEST(test_CFG_08_defaults);
#endif
    RUN_TEST(test_CFG_09_pool_storage);
    RUN_TEST(test_CFG_10_const_handle_is_accepted);
    RUN_TEST(test_CFG_11_status_values);
    return UNITY_END();
}
