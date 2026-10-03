#include "ts_lock.h"

#include "unity.h"

int ts_lock_depth;
unsigned ts_lock_enters;
unsigned ts_lock_nesting_errors;
unsigned ts_lock_token_errors;
unsigned ts_lock_underflow_errors;
void (*ts_lock_exit_hook)(void);

static int ts_lock_token;

int ts_lock_enter(void)
{
    if (ts_lock_depth > 0) {
        ts_lock_nesting_errors++;
    }
    ts_lock_depth++;
    ts_lock_enters++;
    ts_lock_token = (int)(ts_lock_enters & 0x7fffu) + 1;
    return ts_lock_token;
}

void ts_lock_exit(int token)
{
    if (ts_lock_exit_hook != 0) {
        ts_lock_exit_hook();
    }
    if (ts_lock_depth <= 0) {
        ts_lock_underflow_errors++;
        return;
    }
    if (token != ts_lock_token) {
        ts_lock_token_errors++;
    }
    ts_lock_depth--;
}

void ts_lock_reset(void)
{
    ts_lock_depth = 0;
    ts_lock_enters = 0u;
    ts_lock_nesting_errors = 0u;
    ts_lock_token_errors = 0u;
    ts_lock_underflow_errors = 0u;
    ts_lock_exit_hook = 0;
    ts_lock_token = 0;
}

int ts_lock_balanced(void)
{
    return ts_lock_depth == 0 && ts_lock_nesting_errors == 0u && ts_lock_token_errors == 0u &&
           ts_lock_underflow_errors == 0u;
}

void ts_lock_check(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, ts_lock_depth, "lock still held at end of test");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, ts_lock_nesting_errors, "lock was entered recursively");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, ts_lock_token_errors, "lock exited with a wrong token");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, ts_lock_underflow_errors, "lock exited while not held");
}

void ts_lock_expect_enters(unsigned expected, unsigned actual, unsigned line)
{
    if (expected != actual) {
        UnityFail("unexpected number of lock entries", (UNITY_LINE_TYPE)line);
    }
}
