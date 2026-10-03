/* Test configuration "wide": large types, BA < PA */
#ifndef BLOC_TEST_CFG_WIDE_H
#define BLOC_TEST_CFG_WIDE_H

#include <stdint.h>

#define BLOC_BLOCK_ALIGNMENT 1
#define BLOC_PAYLOAD_ALIGNMENT 8
#define BLOC_SIZE_T uint32_t
#define BLOC_COUNT_T uint16_t
#define BLOC_REFCOUNT_T uint16_t
#define BLOC_CHECKS 1
#define BLOC_DEBUG 0
#define BLOC_STATS 1

#include "ts_assert.h"

#define BLOC_PLATFORM_ASSERT(msg) ts_assert_fail(msg)

#define TS_LOCK_TRACER 1
#include "ts_lock.h"

#define BLOC_THREAD_SAFE 1
#define BLOC_DECL_PROTECT(lev) int lev
#define BLOC_PROTECT(lev) ((lev) = ts_lock_enter())
#define BLOC_UNPROTECT(lev) ts_lock_exit(lev)

#endif /* BLOC_TEST_CFG_WIDE_H */
