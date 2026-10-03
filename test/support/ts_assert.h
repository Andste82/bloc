/*
 * Assertion hook used as BLOC_PLATFORM_ASSERT in the test configurations.
 *
 * This header is included from configuration headers and must stay freestanding.
 */
#ifndef TS_ASSERT_H
#define TS_ASSERT_H

/* Target of BLOC_PLATFORM_ASSERT. Records the call and returns (it never aborts). */
void ts_assert_fail(const char *msg);

extern unsigned ts_assert_count;       /* assertions since the last reset or expectation     */
extern unsigned ts_assert_unexpected;  /* assertions fired outside TS_EXPECT_ASSERT           */
extern int ts_assert_lock_depth;       /* lock depth when the last assertion fired            */
extern const char *ts_assert_last_msg; /* message of the last assertion, or NULL              */

/* Clears all assertion bookkeeping. Called from setUp(). */
void ts_assert_reset(void);

/* Fails the running Unity test if an unexpected assertion fired. Called from tearDown(). */
void ts_assert_check(void);

/* Support for the macros below. */
void ts_assert_expect_begin(void);
void ts_assert_expect_end(unsigned line);
void ts_assert_none_begin(void);
void ts_assert_none_end(unsigned line);

/* Runs stmt and asserts that exactly one assertion fired, outside the lock. */
#define TS_EXPECT_ASSERT(stmt)                                                                     \
    do {                                                                                           \
        ts_assert_expect_begin();                                                                  \
        stmt;                                                                                      \
        ts_assert_expect_end((unsigned)__LINE__);                                                  \
    } while (0)

/* Runs stmt and asserts that no assertion fired. */
#define TS_EXPECT_NO_ASSERT(stmt)                                                                  \
    do {                                                                                           \
        ts_assert_none_begin();                                                                    \
        stmt;                                                                                      \
        ts_assert_none_end((unsigned)__LINE__);                                                    \
    } while (0)

#endif /* TS_ASSERT_H */
