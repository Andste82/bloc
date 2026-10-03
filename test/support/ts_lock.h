/*
 * Lock tracer: counting BLOC_PROTECT macros that record depth, nesting and token errors.
 *
 * This header is included from configuration headers and must stay freestanding.
 */
#ifndef TS_LOCK_H
#define TS_LOCK_H

extern int ts_lock_depth;                 /* current lock depth                      */
extern int ts_lock_max_depth;             /* highest depth since the last reset      */
extern unsigned ts_lock_enters;           /* number of ts_lock_enter() calls         */
extern unsigned ts_lock_nesting_errors;   /* enter while already holding the lock    */
extern unsigned ts_lock_token_errors;     /* exit with a wrong token                 */
extern unsigned ts_lock_underflow_errors; /* exit while not holding the lock         */
extern void (*ts_lock_exit_hook)(void);   /* called inside ts_lock_exit before unlock */

int ts_lock_enter(void);
void ts_lock_exit(int token);

/* Clears all counters and the exit hook. Called from setUp(). */
void ts_lock_reset(void);

/* True if the depth is zero and no error was recorded. */
int ts_lock_balanced(void);

/* Fails the running Unity test if the lock is unbalanced. Called from tearDown(). */
void ts_lock_check(void);

/* Support for TS_EXPECT_LOCKS(). */
void ts_lock_expect_enters(unsigned expected, unsigned actual, unsigned line);

/* Runs stmt and asserts that it entered the lock exactly n times. */
#define TS_EXPECT_LOCKS(n, stmt)                                                                   \
    do {                                                                                           \
        unsigned ts_lock_before_ = ts_lock_enters;                                                 \
        stmt;                                                                                      \
        ts_lock_expect_enters((unsigned)(n), ts_lock_enters - ts_lock_before_,                     \
                              (unsigned)__LINE__);                                                 \
    } while (0)

/*
 * The tracing configurations (debug, wide) define TS_LOCK_TRACER and map BLOC_DECL_PROTECT,
 * BLOC_PROTECT and BLOC_UNPROTECT onto ts_lock_enter() and ts_lock_exit() in their own header,
 * so the result does not depend on the order in which headers are included.
 */

#endif /* TS_LOCK_H */
