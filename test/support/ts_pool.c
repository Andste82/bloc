/*
 * Kept apart from ts_helpers.c so that tests which do not set up pools never link against the
 * library's pool functions.
 */
#include "ts_arena.h"
#include "ts_helpers.h"
#include "unity.h"

void ts_pool_setup(bloc_pool_t *p, bloc_count_t n, bloc_size_t e)
{
    size_t size = ts_expected_stride((size_t)e) * (size_t)n;
    uint8_t *storage = ts_storage(size, 0u);

    TEST_ASSERT_NOT_NULL(storage);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_init(p, storage, size, n, e));
}
