/*
 * Self-tests of the test support library: guard band detection, assertion bookkeeping, lock
 * tracer errors and the small helpers. Every other test file relies on these behaving exactly as
 * described in the implementation plan, section 5.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bloc.h"
#include "ts_arena.h"
#include "ts_assert.h"
#include "ts_helpers.h"
#include "ts_lock.h"
#include "ts_output.h"
#include "unity.h"

#if BLOC_THREAD_SAFE && !defined(TS_LOCK_TRACER)
#include <pthread.h>

#include "ts_pthread.h"
#endif

/* Variables touched by statements inside TS_EXPECT_FAIL() must not live on the stack. */
static uint8_t *g_region;
static int g_token;
static int g_seen_depth;

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

/* --- Guarded storage arena --------------------------------------------------------------- */

void test_TS_arena_alignment_fill_and_guards(void)
{
    static const size_t misalign[] = {0u, 1u, 3u, 7u};
    size_t i;
    size_t k;

    for (i = 0u; i < sizeof(misalign) / sizeof(misalign[0]); i++) {
        const size_t size = 100u;
        uint8_t *p = ts_storage(size, misalign[i]);

        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_EQUAL_UINT(misalign[i] % ts_expected_storage_alignment(),
                               (size_t)((uintptr_t)p % ts_expected_storage_alignment()));
        for (k = 0u; k < size; k++) {
            TEST_ASSERT_EQUAL_HEX8(TS_ARENA_FILL_BYTE, p[k]);
        }
        for (k = 1u; k <= TS_ARENA_GUARD; k++) {
            TEST_ASSERT_EQUAL_HEX8(TS_ARENA_GUARD_BYTE, p[-(ptrdiff_t)k]);
            TEST_ASSERT_EQUAL_HEX8(TS_ARENA_GUARD_BYTE, p[size + k - 1u]);
        }
    }
    TEST_ASSERT_EQUAL_UINT(sizeof(misalign) / sizeof(misalign[0]), ts_arena_region_count());
    TEST_ASSERT_TRUE(ts_arena_guards_ok() != 0);
}

void test_TS_arena_regions_are_separated_by_guards(void)
{
    uint8_t *a = ts_storage(10u, 0u);
    uint8_t *b = ts_storage(10u, 0u);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(b >= a + 10u + 2u * TS_ARENA_GUARD);
}

void test_TS_arena_writes_inside_region_keep_guards_intact(void)
{
    uint8_t *p = ts_storage(32u, 0u);

    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0, 32u);
    TEST_ASSERT_TRUE(ts_arena_guards_ok() != 0);
}

void test_TS_arena_detects_guard_corruption(void)
{
    uint8_t *a = ts_storage(32u, 0u);
    uint8_t *b = ts_storage(32u, 3u);
    const size_t size = 32u;

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    a[-1] = 0u; /* underrun by one byte */
    TEST_ASSERT_TRUE(ts_arena_guards_ok() == 0);
    a[-1] = (uint8_t)TS_ARENA_GUARD_BYTE;

    a[size] = 0u; /* overrun by one byte */
    TEST_ASSERT_TRUE(ts_arena_guards_ok() == 0);
    a[size] = (uint8_t)TS_ARENA_GUARD_BYTE;

    a[-(ptrdiff_t)TS_ARENA_GUARD] = 0u; /* outermost byte of the lower guard */
    TEST_ASSERT_TRUE(ts_arena_guards_ok() == 0);
    a[-(ptrdiff_t)TS_ARENA_GUARD] = (uint8_t)TS_ARENA_GUARD_BYTE;

    a[size + TS_ARENA_GUARD - 1u] = 0u; /* outermost byte of the upper guard */
    TEST_ASSERT_TRUE(ts_arena_guards_ok() == 0);
    a[size + TS_ARENA_GUARD - 1u] = (uint8_t)TS_ARENA_GUARD_BYTE;

    b[size] = 0u; /* the last region is checked too */
    TEST_ASSERT_TRUE(ts_arena_guards_ok() == 0);
    b[size] = (uint8_t)TS_ARENA_GUARD_BYTE;

    TEST_ASSERT_TRUE(ts_arena_guards_ok() != 0);
}

void test_TS_arena_check_fails_on_corruption(void)
{
    g_region = ts_storage(16u, 0u);
    TEST_ASSERT_NOT_NULL(g_region);

    TS_EXPECT_FAIL({
        g_region[16] = 0u;
        ts_arena_check();
    });
    g_region[16] = (uint8_t)TS_ARENA_GUARD_BYTE;
    ts_arena_check();
}

void test_TS_arena_reset_reuses_the_arena(void)
{
    uint8_t *first = ts_storage(64u, 0u);

    TEST_ASSERT_NOT_NULL(first);
    ts_arena_reset();
    TEST_ASSERT_EQUAL_UINT(0u, ts_arena_region_count());
    TEST_ASSERT_TRUE(ts_storage(64u, 0u) == first);
}

void test_TS_arena_exhaustion_returns_null(void)
{
    const size_t exact = TS_ARENA_SIZE - 2u * TS_ARENA_GUARD;
    size_t i;

    TEST_ASSERT_TRUE(BLOC_STORAGE_ALIGNMENT <= TS_ARENA_GUARD);
    TEST_ASSERT_NULL(ts_storage(TS_ARENA_SIZE, 0u));
    TEST_ASSERT_NULL(ts_storage((size_t)-1, 0u));
    TEST_ASSERT_NULL(ts_storage(1u, TS_ARENA_SIZE + 1u));
    TEST_ASSERT_EQUAL_UINT(0u, ts_arena_region_count());

    TEST_ASSERT_NULL(ts_storage(exact + 1u, 0u));
    TEST_ASSERT_NOT_NULL(ts_storage(exact, 0u));
    TEST_ASSERT_NULL(ts_storage(1u, 0u));

    ts_arena_reset();
    for (i = 0u; i < TS_ARENA_MAX_REGIONS; i++) {
        TEST_ASSERT_NOT_NULL(ts_storage(1u, 0u));
    }
    TEST_ASSERT_NULL(ts_storage(1u, 0u));
    TEST_ASSERT_EQUAL_UINT(TS_ARENA_MAX_REGIONS, ts_arena_region_count());
}

/* --- Assertion hook ---------------------------------------------------------------------- */

void test_TS_assert_hook_records_and_returns(void)
{
    ts_assert_fail("first");
    TEST_ASSERT_EQUAL_UINT(1u, ts_assert_count);
    TEST_ASSERT_EQUAL_UINT(1u, ts_assert_unexpected);
    TEST_ASSERT_EQUAL_STRING("first", ts_assert_last_msg);

    ts_assert_fail("second");
    TEST_ASSERT_EQUAL_UINT(2u, ts_assert_count);
    TEST_ASSERT_EQUAL_UINT(2u, ts_assert_unexpected);
    TEST_ASSERT_EQUAL_STRING("second", ts_assert_last_msg);

    TS_EXPECT_FAIL(ts_assert_check());

    ts_assert_reset();
    TEST_ASSERT_EQUAL_UINT(0u, ts_assert_count);
    TEST_ASSERT_EQUAL_UINT(0u, ts_assert_unexpected);
    TEST_ASSERT_NULL(ts_assert_last_msg);
    ts_assert_check();
}

void test_TS_assert_expect_assert_accepts_exactly_one(void)
{
    TS_EXPECT_ASSERT(ts_assert_fail("expected"));
    TEST_ASSERT_EQUAL_UINT(1u, ts_assert_count);
    TEST_ASSERT_EQUAL_UINT(0u, ts_assert_unexpected);
    TEST_ASSERT_EQUAL_INT(0, ts_assert_lock_depth);
    TEST_ASSERT_EQUAL_STRING("expected", ts_assert_last_msg);

    /* the counter restarts for every expectation */
    TS_EXPECT_ASSERT(ts_assert_fail("again"));
    TEST_ASSERT_EQUAL_UINT(0u, ts_assert_unexpected);

    /* an assertion after the expectation is unexpected again */
    ts_assert_fail("late");
    TEST_ASSERT_EQUAL_UINT(1u, ts_assert_unexpected);
    ts_assert_reset();
}

void test_TS_assert_expect_assert_rejects_none_and_two(void)
{
    TS_EXPECT_FAIL(TS_EXPECT_ASSERT((void)0));
    TEST_ASSERT_EQUAL_UINT(0u, ts_assert_count);

    TS_EXPECT_FAIL(TS_EXPECT_ASSERT({
        ts_assert_fail("one");
        ts_assert_fail("two");
    }));
    TEST_ASSERT_EQUAL_UINT(2u, ts_assert_count);

    ts_assert_reset();
}

void test_TS_assert_expect_assert_rejects_assert_under_lock(void)
{
    TS_EXPECT_FAIL(TS_EXPECT_ASSERT({
        g_token = ts_lock_enter();
        ts_assert_fail("under lock");
        ts_lock_exit(g_token);
    }));
    TEST_ASSERT_EQUAL_INT(1, ts_assert_lock_depth);
    TEST_ASSERT_EQUAL_INT(0, ts_lock_depth);

    ts_assert_reset();
    ts_lock_reset();
}

void test_TS_assert_check_fails_for_assertion_under_lock(void)
{
    g_token = ts_lock_enter();
    ts_assert_fail("under lock");
    ts_lock_exit(g_token);
    TEST_ASSERT_EQUAL_UINT(1u, ts_assert_locked);
    /* even an assertion that was expected is a failure when it fired with the lock held */
    ts_assert_unexpected = 0u;
    TS_EXPECT_FAIL(ts_assert_check());

    ts_assert_reset();
    TEST_ASSERT_EQUAL_UINT(0u, ts_assert_locked);
    ts_assert_check();
}

void test_TS_lock_tracks_the_maximum_depth(void)
{
    TEST_ASSERT_EQUAL_INT(0, ts_lock_max_depth);
    g_token = ts_lock_enter();
    ts_lock_exit(g_token);
    TEST_ASSERT_EQUAL_INT(1, ts_lock_max_depth);

    g_token = ts_lock_enter();
    (void)ts_lock_enter(); /* nesting error, depth 2 */
    TEST_ASSERT_EQUAL_INT(2, ts_lock_max_depth);
    ts_lock_reset();
    TEST_ASSERT_EQUAL_INT(0, ts_lock_max_depth);
    ts_lock_check();
}

void test_TS_assert_expect_no_assert(void)
{
    TS_EXPECT_NO_ASSERT((void)0);
    TEST_ASSERT_EQUAL_UINT(0u, ts_assert_count);

    TS_EXPECT_FAIL(TS_EXPECT_NO_ASSERT(ts_assert_fail("unwanted")));
    TEST_ASSERT_EQUAL_UINT(1u, ts_assert_unexpected);

    ts_assert_reset();
}

/* --- Lock tracer ------------------------------------------------------------------------- */

void test_TS_lock_enter_exit_is_balanced(void)
{
    int token = ts_lock_enter();

    TEST_ASSERT_EQUAL_INT(1, ts_lock_depth);
    TEST_ASSERT_EQUAL_UINT(1u, ts_lock_enters);
    TEST_ASSERT_TRUE(ts_lock_balanced() == 0);
    ts_lock_exit(token);
    TEST_ASSERT_EQUAL_INT(0, ts_lock_depth);
    TEST_ASSERT_TRUE(ts_lock_balanced() != 0);

    token = ts_lock_enter();
    ts_lock_exit(token);
    TEST_ASSERT_EQUAL_UINT(2u, ts_lock_enters);
    TEST_ASSERT_TRUE(ts_lock_balanced() != 0);
    ts_lock_check();
}

void test_TS_lock_detects_nesting(void)
{
    int outer = ts_lock_enter();
    int inner = ts_lock_enter();

    TEST_ASSERT_EQUAL_UINT(1u, ts_lock_nesting_errors);
    TEST_ASSERT_EQUAL_INT(2, ts_lock_depth);
    ts_lock_exit(inner);
    ts_lock_exit(outer);
    TEST_ASSERT_EQUAL_INT(0, ts_lock_depth);
    TEST_ASSERT_TRUE(ts_lock_balanced() == 0);

    TS_EXPECT_FAIL(ts_lock_check());
    ts_lock_reset();
    ts_lock_check();
}

void test_TS_lock_detects_wrong_token(void)
{
    int token = ts_lock_enter();

    ts_lock_exit(token + 1);
    TEST_ASSERT_EQUAL_UINT(1u, ts_lock_token_errors);
    TEST_ASSERT_EQUAL_INT(0, ts_lock_depth);
    TEST_ASSERT_TRUE(ts_lock_balanced() == 0);

    TS_EXPECT_FAIL(ts_lock_check());
    ts_lock_reset();
}

void test_TS_lock_detects_exit_without_enter(void)
{
    ts_lock_exit(1);
    TEST_ASSERT_EQUAL_UINT(1u, ts_lock_underflow_errors);
    TEST_ASSERT_EQUAL_INT(0, ts_lock_depth);
    TEST_ASSERT_TRUE(ts_lock_balanced() == 0);

    TS_EXPECT_FAIL(ts_lock_check());
    ts_lock_reset();
}

void test_TS_lock_check_fails_while_held(void)
{
    TS_EXPECT_FAIL({
        g_token = ts_lock_enter();
        ts_lock_check();
    });
    TEST_ASSERT_EQUAL_INT(1, ts_lock_depth);
    ts_lock_exit(g_token);
    ts_lock_check();
}

static void record_depth(void) { g_seen_depth = ts_lock_depth; }

void test_TS_lock_exit_hook_runs_before_unlock(void)
{
    int token;

    g_seen_depth = -1;
    ts_lock_exit_hook = record_depth;
    token = ts_lock_enter();
    ts_lock_exit(token);
    TEST_ASSERT_EQUAL_INT(1, g_seen_depth);
    TEST_ASSERT_EQUAL_INT(0, ts_lock_depth);

    ts_lock_reset();
    TEST_ASSERT_NULL(ts_lock_exit_hook);
}

void test_TS_lock_expect_locks_counts_entries(void)
{
    TS_EXPECT_LOCKS(0, (void)0);
    TS_EXPECT_LOCKS(2, {
        g_token = ts_lock_enter();
        ts_lock_exit(g_token);
        g_token = ts_lock_enter();
        ts_lock_exit(g_token);
    });
    TS_EXPECT_FAIL(TS_EXPECT_LOCKS(1, (void)0));
}

#ifdef TS_LOCK_TRACER
void test_TS_lock_tracer_macros_map_to_the_tracer(void)
{
    BLOC_DECL_PROTECT(lev);

    BLOC_PROTECT(lev);
    TEST_ASSERT_EQUAL_INT(1, ts_lock_depth);
    BLOC_UNPROTECT(lev);
    TEST_ASSERT_EQUAL_INT(0, ts_lock_depth);
    TEST_ASSERT_EQUAL_UINT(1u, ts_lock_enters);
}
#endif

/* --- pthread configuration --------------------------------------------------------------- */

#if BLOC_THREAD_SAFE && !defined(TS_LOCK_TRACER)
#define TS_THREAD_ROUNDS 20000u

static unsigned g_shared_counter;

static void *increment_worker(void *arg)
{
    unsigned i;

    (void)arg;
    for (i = 0u; i < TS_THREAD_ROUNDS; i++) {
        BLOC_DECL_PROTECT(lev);
        BLOC_PROTECT(lev);
        g_shared_counter++;
        BLOC_UNPROTECT(lev);
    }
    return NULL;
}

void test_TS_pthread_protect_macros_exclude_each_other(void)
{
    pthread_t a;
    pthread_t b;
    unsigned locks_before = ts_pthread_locks;

    g_shared_counter = 0u;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&a, NULL, increment_worker, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&b, NULL, increment_worker, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(a, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(b, NULL));

    TEST_ASSERT_EQUAL_UINT(2u * TS_THREAD_ROUNDS, g_shared_counter);
    TEST_ASSERT_EQUAL_UINT(2u * TS_THREAD_ROUNDS, ts_pthread_locks - locks_before);
    TEST_ASSERT_EQUAL_UINT(ts_pthread_locks, ts_pthread_unlocks);
}
#endif

/* --- Helpers ----------------------------------------------------------------------------- */

void test_TS_fill_and_check(void)
{
    uint8_t buf[40];

    ts_fill(buf, sizeof(buf), 250u);
    TEST_ASSERT_EQUAL_UINT8(250u, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(251u, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, buf[6]); /* wraps around */
    TEST_ASSERT_TRUE(ts_fill_matches(buf, sizeof(buf), 250u) != 0);
    TEST_ASSERT_TRUE(ts_fill_matches(buf, sizeof(buf), 251u) == 0);
    TEST_ASSERT_TRUE(ts_fill_matches(buf, 0u, 7u) != 0);
    ts_check_fill(buf, sizeof(buf), 250u);

    buf[sizeof(buf) - 1u] ^= 0x01u;
    TEST_ASSERT_TRUE(ts_fill_matches(buf, sizeof(buf), 250u) == 0);
    TEST_ASSERT_TRUE(ts_fill_matches(buf, sizeof(buf) - 1u, 250u) != 0);
}

static uint8_t g_fill_buf[8];

void test_TS_check_fill_fails_on_mismatch(void)
{
    ts_fill(g_fill_buf, sizeof(g_fill_buf), 1u);
    g_fill_buf[3] = 0xEEu;
    TS_EXPECT_FAIL(ts_check_fill(g_fill_buf, sizeof(g_fill_buf), 1u));
}

static uint8_t g_same_a[8];
static uint8_t g_same_b[8];

void test_TS_check_same_outside(void)
{
    ts_fill(g_same_a, sizeof(g_same_a), 7u);
    memcpy(g_same_b, g_same_a, sizeof(g_same_b));
    g_same_b[3] = 0xEEu;
    g_same_b[4] = 0xEFu;
    ts_check_same_outside(g_same_b, g_same_a, sizeof(g_same_a), 3u, 5u);
    TS_EXPECT_FAIL(ts_check_same_outside(g_same_b, g_same_a, sizeof(g_same_a), 4u, 5u));
    TS_EXPECT_FAIL(ts_check_same_outside(g_same_b, g_same_a, sizeof(g_same_a), 3u, 4u));
    TS_EXPECT_FAIL(ts_check_same_outside(g_same_b, g_same_a, sizeof(g_same_a), 0u, 0u));
}

void test_TS_xorshift32_is_deterministic(void)
{
    uint32_t state = 1u;

    TEST_ASSERT_EQUAL_UINT32(270369u, ts_xorshift32(&state));
    TEST_ASSERT_EQUAL_UINT32(270369u, state);
    TEST_ASSERT_EQUAL_UINT32(67634689u, ts_xorshift32(&state));
    TEST_ASSERT_EQUAL_UINT32(2647435461u, ts_xorshift32(&state));

    state = 1u;
    TEST_ASSERT_EQUAL_UINT32(270369u, ts_xorshift32(&state));
}

void test_TS_expected_layout_helpers(void)
{
    const size_t sa = ts_expected_storage_alignment();
    const size_t header = ts_expected_header();
    size_t e;

    TEST_ASSERT_TRUE(sa != 0u && (sa & (sa - 1u)) == 0u);
    TEST_ASSERT_TRUE(sa >= (size_t)BLOC_BLOCK_ALIGNMENT);
    TEST_ASSERT_TRUE(sa >= (size_t)BLOC_PAYLOAD_ALIGNMENT);
    TEST_ASSERT_TRUE(sa >= _Alignof(struct bloc_handle));
    TEST_ASSERT_TRUE(header >= sizeof(struct bloc_handle));
    TEST_ASSERT_TRUE(header - sizeof(struct bloc_handle) < (size_t)BLOC_PAYLOAD_ALIGNMENT);
    TEST_ASSERT_EQUAL_UINT(0u, header % (size_t)BLOC_PAYLOAD_ALIGNMENT);

    for (e = 0u; e <= 300u; e++) {
        const size_t stride = ts_expected_stride(e);

        TEST_ASSERT_EQUAL_UINT(0u, stride % sa);
        TEST_ASSERT_TRUE(stride >= header + e);
        TEST_ASSERT_TRUE(stride - (header + e) < sa);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_TS_arena_alignment_fill_and_guards);
    RUN_TEST(test_TS_arena_regions_are_separated_by_guards);
    RUN_TEST(test_TS_arena_writes_inside_region_keep_guards_intact);
    RUN_TEST(test_TS_arena_detects_guard_corruption);
    RUN_TEST(test_TS_arena_check_fails_on_corruption);
    RUN_TEST(test_TS_arena_reset_reuses_the_arena);
    RUN_TEST(test_TS_arena_exhaustion_returns_null);
    RUN_TEST(test_TS_assert_hook_records_and_returns);
    RUN_TEST(test_TS_assert_expect_assert_accepts_exactly_one);
    RUN_TEST(test_TS_assert_expect_assert_rejects_none_and_two);
    RUN_TEST(test_TS_assert_expect_assert_rejects_assert_under_lock);
    RUN_TEST(test_TS_assert_expect_no_assert);
    RUN_TEST(test_TS_assert_check_fails_for_assertion_under_lock);
    RUN_TEST(test_TS_lock_tracks_the_maximum_depth);
    RUN_TEST(test_TS_lock_enter_exit_is_balanced);
    RUN_TEST(test_TS_lock_detects_nesting);
    RUN_TEST(test_TS_lock_detects_wrong_token);
    RUN_TEST(test_TS_lock_detects_exit_without_enter);
    RUN_TEST(test_TS_lock_check_fails_while_held);
    RUN_TEST(test_TS_lock_exit_hook_runs_before_unlock);
    RUN_TEST(test_TS_lock_expect_locks_counts_entries);
#ifdef TS_LOCK_TRACER
    RUN_TEST(test_TS_lock_tracer_macros_map_to_the_tracer);
#endif
#if BLOC_THREAD_SAFE && !defined(TS_LOCK_TRACER)
    RUN_TEST(test_TS_pthread_protect_macros_exclude_each_other);
#endif
    RUN_TEST(test_TS_fill_and_check);
    RUN_TEST(test_TS_check_fill_fails_on_mismatch);
    RUN_TEST(test_TS_check_same_outside);
    RUN_TEST(test_TS_xorshift32_is_deterministic);
    RUN_TEST(test_TS_expected_layout_helpers);
    return UNITY_END();
}
