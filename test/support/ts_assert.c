#include "ts_assert.h"

#include "ts_lock.h"
#include "unity.h"

unsigned ts_assert_count;
unsigned ts_assert_unexpected;
int ts_assert_lock_depth;
const char *ts_assert_last_msg;

static int ts_assert_expected;

void ts_assert_fail(const char *msg)
{
    ts_assert_count++;
    ts_assert_last_msg = msg;
    ts_assert_lock_depth = ts_lock_depth;
    if (!ts_assert_expected) {
        ts_assert_unexpected++;
    }
}

void ts_assert_reset(void)
{
    ts_assert_count = 0u;
    ts_assert_unexpected = 0u;
    ts_assert_lock_depth = 0;
    ts_assert_last_msg = 0;
    ts_assert_expected = 0;
}

void ts_assert_check(void)
{
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, ts_assert_unexpected, "unexpected BLOC assertion");
}

void ts_assert_expect_begin(void)
{
    ts_assert_count = 0u;
    ts_assert_lock_depth = 0;
    ts_assert_last_msg = 0;
    ts_assert_expected = 1;
}

void ts_assert_expect_end(unsigned line)
{
    ts_assert_expected = 0;
    if (ts_assert_count != 1u) {
        UnityFail("expected exactly one BLOC assertion", (UNITY_LINE_TYPE)line);
    }
    if (ts_assert_lock_depth != 0) {
        UnityFail("BLOC assertion fired while the lock was held", (UNITY_LINE_TYPE)line);
    }
}

void ts_assert_none_begin(void)
{
    ts_assert_count = 0u;
    ts_assert_last_msg = 0;
    ts_assert_expected = 0;
}

void ts_assert_none_end(unsigned line)
{
    if (ts_assert_count != 0u) {
        UnityFail("unexpected BLOC assertion", (UNITY_LINE_TYPE)line);
    }
}
