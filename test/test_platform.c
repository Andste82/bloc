/*
 * EM-04: the executing machine matches the target the build claims to be for.
 *
 * Built only when BLOC_EXPECT_BIG_ENDIAN and BLOC_EXPECT_PTR_BITS are defined (by
 * scripts/ci/build_one.sh from the target table), so every emulated job proves that it really
 * ran code of the architecture it names.
 */
#include <limits.h>
#include <stdint.h>

#include "ts_helpers.h"
#include "unity.h"

#if !defined(BLOC_EXPECT_BIG_ENDIAN) || !defined(BLOC_EXPECT_PTR_BITS)
#error "test_platform.c needs BLOC_EXPECT_BIG_ENDIAN and BLOC_EXPECT_PTR_BITS"
#endif

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

void test_EM_04_byte_order(void)
{
    union {
        uint32_t word;
        uint8_t bytes[4];
    } probe;
    int big_endian;

    probe.word = 0x01020304u;
    big_endian = probe.bytes[0] == 0x01u;
    TEST_ASSERT_TRUE(big_endian || probe.bytes[0] == 0x04u);
    TEST_ASSERT_EQUAL_INT(BLOC_EXPECT_BIG_ENDIAN, big_endian);
}

void test_EM_04_pointer_width(void)
{
    TEST_ASSERT_EQUAL_UINT((unsigned)BLOC_EXPECT_PTR_BITS, (unsigned)(sizeof(void *) * CHAR_BIT));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_EM_04_byte_order);
    RUN_TEST(test_EM_04_pointer_width);
    return UNITY_END();
}
