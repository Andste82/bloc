#include "ts_helpers.h"

#include "ts_arena.h"
#include "ts_assert.h"
#include "ts_lock.h"
#include "unity.h"

void ts_fill(uint8_t *p, size_t n, uint8_t seed)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        p[i] = (uint8_t)(seed + i);
    }
}

int ts_fill_matches(const uint8_t *p, size_t n, uint8_t seed)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        if (p[i] != (uint8_t)(seed + i)) {
            return 0;
        }
    }
    return 1;
}

void ts_check_fill(const uint8_t *p, size_t n, uint8_t seed)
{
    TEST_ASSERT_TRUE_MESSAGE(ts_fill_matches(p, n, seed) != 0, "fill pattern mismatch");
}

uint32_t ts_xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Smallest multiple of a that is >= x, found by counting instead of by bit masks. */
static size_t ts_round_up(size_t x, size_t a)
{
    while (x % a != 0u) {
        x++;
    }
    return x;
}

size_t ts_expected_storage_alignment(void)
{
    size_t a = (size_t)BLOC_BLOCK_ALIGNMENT;
    if ((size_t)BLOC_PAYLOAD_ALIGNMENT > a) {
        a = (size_t)BLOC_PAYLOAD_ALIGNMENT;
    }
    if (_Alignof(struct bloc_handle) > a) {
        a = _Alignof(struct bloc_handle);
    }
    return a;
}

size_t ts_expected_header(void)
{
    return ts_round_up(sizeof(struct bloc_handle), (size_t)BLOC_PAYLOAD_ALIGNMENT);
}

size_t ts_expected_stride(size_t element_size)
{
    return ts_round_up(ts_expected_header() + element_size, ts_expected_storage_alignment());
}

void ts_test_setup(void)
{
    ts_arena_reset();
    ts_assert_reset();
    ts_lock_reset();
}

void ts_test_teardown(void)
{
    ts_arena_check();
    ts_assert_check();
    ts_lock_check();
}
