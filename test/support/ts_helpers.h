/* Pattern, PRNG, pool-setup and layout helpers for the tests. */
#ifndef TS_HELPERS_H
#define TS_HELPERS_H

#include <stddef.h>
#include <stdint.h>

#include "bloc/bloc.h"
#include "ts_assert.h"
#include "ts_lock.h"

/* 1 if the lock tracer is active: thread safety is on and the configuration uses ts_lock. */
#if BLOC_THREAD_SAFE && defined(TS_LOCK_TRACER)
#define TS_HAVE_TRACER 1
#else
#define TS_HAVE_TRACER 0
#endif

/* Runs stmt and, where the lock tracer is active, asserts that it entered the lock n times. */
#if TS_HAVE_TRACER
#define TS_LOCKS(n, stmt) TS_EXPECT_LOCKS(n, stmt)
#else
#define TS_LOCKS(n, stmt) stmt
#endif

/*
 * Runs stmt, which must trigger a BLOC check that asserts in debug builds: exactly one assertion
 * (outside the lock) in a BLOC_DEBUG configuration, none otherwise.
 */
#if BLOC_DEBUG
#define TS_CHK_ASSERT(stmt) TS_EXPECT_ASSERT(stmt)
#else
#define TS_CHK_ASSERT(stmt) TS_EXPECT_NO_ASSERT(stmt)
#endif

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

/* Block i of an initialized pool, computed from the expected stride and the storage base. */
struct bloc_handle *ts_block(const bloc_pool_t *p, size_t i);

/* Start of the data area of a block (header size from first principles), headroom included. */
uint8_t *ts_area(const struct bloc_handle *b);

/* Fails the running Unity test if p[i] != before[i] for any i in [0, size) outside [from, to). */
void ts_check_same_outside(const uint8_t *p, const uint8_t *before, size_t size, size_t from,
                           size_t to);

/* Largest of the values 32 and 8 * BLOC_PAYLOAD_ALIGNMENT: an element size that is a multiple of
 * PA. */
size_t ts_element_size_aligned(void);

/* Smallest multiple of the payload alignment that is >= x, computed without bit masks. */
size_t ts_round_up_pa(size_t x);

/* setUp() / tearDown() bodies shared by every test file. */
void ts_test_setup(void);
void ts_test_teardown(void);

#endif /* TS_HELPERS_H */
