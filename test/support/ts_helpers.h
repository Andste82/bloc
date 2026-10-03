/* Pattern, PRNG, pool-setup and layout helpers for the tests. */
#ifndef TS_HELPERS_H
#define TS_HELPERS_H

#include <stddef.h>
#include <stdint.h>

#include "bloc.h"

/* Writes the pattern seed, seed+1, ... into p[0..n). */
void ts_fill(uint8_t *p, size_t n, uint8_t seed);

/* True if p[0..n) holds the pattern written by ts_fill(p, n, seed). */
int ts_fill_matches(const uint8_t *p, size_t n, uint8_t seed);

/* Fails the running Unity test if p[0..n) does not hold the ts_fill pattern. */
void ts_check_fill(const uint8_t *p, size_t n, uint8_t seed);

/* Deterministic xorshift32 PRNG (shifts 13, 17, 5). The state must be non-zero. */
uint32_t ts_xorshift32(uint32_t *state);

/* ts_storage() + bloc_pool_init() + a Unity assertion on the result. */
void ts_pool_setup(bloc_pool_t *p, bloc_count_t n, bloc_size_t e);

/*
 * Layout values computed from first principles (sizeof, _Alignof, spec formulas) and
 * independent of the BLOC layout macros.
 */
size_t ts_expected_storage_alignment(void);
size_t ts_expected_header(void);
size_t ts_expected_stride(size_t element_size);

/* setUp() / tearDown() bodies shared by every test file. */
void ts_test_setup(void);
void ts_test_teardown(void);

#endif /* TS_HELPERS_H */
