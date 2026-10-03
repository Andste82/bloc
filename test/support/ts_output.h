/*
 * Unity output hook and support for testing the test support library itself.
 *
 * Unity is built with UNITY_OUTPUT_CHAR=ts_unity_putchar, which forwards to putchar() unless an
 * expected failure is being trapped. Every test executable must therefore be linked with
 * ts_output.c (the CMake function bloc_add_test() does that).
 */
#ifndef TS_OUTPUT_H
#define TS_OUTPUT_H

#include "unity.h"

void ts_unity_putchar(int c);

/* Support for TS_EXPECT_FAIL(); do not call directly. */
void ts_fail_trap_enter(void);
void ts_fail_trap_leave(unsigned line);

/*
 * Runs stmt, which must make Unity fail the test, swallows that failure and its output, and
 * fails the test if stmt completes normally. Does not nest. Variables of the enclosing function
 * that stmt modifies must be static or volatile (setjmp rules).
 */
#define TS_EXPECT_FAIL(stmt)                                                                       \
    do {                                                                                           \
        ts_fail_trap_enter();                                                                      \
        if (TEST_PROTECT()) {                                                                      \
            stmt;                                                                                  \
        }                                                                                          \
        ts_fail_trap_leave((unsigned)__LINE__);                                                    \
    } while (0)

#endif /* TS_OUTPUT_H */
