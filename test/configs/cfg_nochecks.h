/* Test configuration "nochecks": minimal build, only the always-on checks */
#ifndef BLOC_TEST_CFG_NOCHECKS_H
#define BLOC_TEST_CFG_NOCHECKS_H

#include <stdint.h>

#define BLOC_BLOCK_ALIGNMENT 4
#define BLOC_PAYLOAD_ALIGNMENT 4
#define BLOC_SIZE_T uint16_t
#define BLOC_COUNT_T uint8_t
#define BLOC_REFCOUNT_T uint8_t
#define BLOC_CHECKS 0
#define BLOC_DEBUG 0
#define BLOC_STATS 0

#include "ts_assert.h"

#define BLOC_PLATFORM_ASSERT(msg) ts_assert_fail(msg)

#define BLOC_THREAD_SAFE 0

#endif /* BLOC_TEST_CFG_NOCHECKS_H */
