/* Test configuration "pthread": thread stress test with a pthread mutex (TSan only) */
#ifndef BLOC_TEST_CFG_PTHREAD_H
#define BLOC_TEST_CFG_PTHREAD_H

#include <stdint.h>

#define BLOC_BLOCK_ALIGNMENT 4
#define BLOC_PAYLOAD_ALIGNMENT 4
#define BLOC_SIZE_T uint16_t
#define BLOC_COUNT_T uint16_t
#define BLOC_REFCOUNT_T uint16_t
#define BLOC_CHECKS 1
#define BLOC_DEBUG 0
#define BLOC_STATS 0

#include "ts_assert.h"

#define BLOC_PLATFORM_ASSERT(msg) ts_assert_fail(msg)

#include "ts_pthread.h"

#define BLOC_THREAD_SAFE 1
#define BLOC_DECL_PROTECT(lev) int lev
#define BLOC_PROTECT(lev)                                                                          \
    do {                                                                                           \
        (lev) = 0;                                                                                 \
        ts_pthread_lock();                                                                         \
    } while (0)
#define BLOC_UNPROTECT(lev)                                                                        \
    do {                                                                                           \
        (void)(lev);                                                                               \
        ts_pthread_unlock();                                                                       \
    } while (0)

#endif /* BLOC_TEST_CFG_PTHREAD_H */
