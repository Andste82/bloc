/* Test configuration "bigalign": BA > PA, cache-line blocks, 32-bit reference count */
#ifndef BLOC_TEST_CFG_BIGALIGN_H
#define BLOC_TEST_CFG_BIGALIGN_H

#include <stdint.h>

#define BLOC_BLOCK_ALIGNMENT 64
#define BLOC_PAYLOAD_ALIGNMENT 16
#define BLOC_SIZE_T uint16_t
#define BLOC_COUNT_T uint8_t
#define BLOC_REFCOUNT_T uint32_t
#define BLOC_CHECKS 1
#define BLOC_DEBUG 1
#define BLOC_STATS 0

#include "ts_assert.h"

#define BLOC_PLATFORM_ASSERT(msg) ts_assert_fail(msg)

#define BLOC_THREAD_SAFE 0

#endif /* BLOC_TEST_CFG_BIGALIGN_H */
