/*
 * Thread stress test (TS-04, implementation plan section 8.11): several threads hammer one pool
 * through the pthread mutex of the pthread configuration. It is meant to run under
 * ThreadSanitizer (scripts/sanitize.sh --tsan) and is not coverage-gated.
 *
 * Every thread owns exactly one block at a time, so with at least as many blocks as threads no
 * allocation can fail, and a block is touched by one thread only between its alloc and its final
 * release. Unity is not thread-safe: the worker threads only count errors, and the main thread
 * evaluates them after joining.
 */
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "bloc.h"
#include "ts_arena.h"
#include "ts_helpers.h"
#include "unity.h"

#if BLOC_THREAD_SAFE && !defined(TS_LOCK_TRACER)

#define STRESS_THREADS 8u
#define STRESS_ITERATIONS 100000u
#define STRESS_BLOCKS 16u
#define STRESS_ELEMENT 32u

struct worker {
    pthread_t thread;
    bloc_pool_t *pool;
    unsigned id;
    unsigned alloc_failures; /* alloc returned NULL            */
    unsigned status_errors;  /* an operation returned an error */
    unsigned mismatches;     /* the pattern did not survive    */
    unsigned long done;      /* completed iterations           */
};

static void *worker_main(void *arg)
{
    struct worker *w = (struct worker *)arg;
    unsigned long i;

    for (i = 0u; i < STRESS_ITERATIONS; i++) {
        const uint8_t seed = (uint8_t)(w->id * 31u + i);
        bloc_handle_t b = bloc_alloc(w->pool, 0u);

        if (b == NULL) {
            w->alloc_failures++;
            continue;
        }
        if (bloc_set_len(b, STRESS_ELEMENT) != BLOC_OK) {
            w->status_errors++;
        }
        ts_fill((uint8_t *)bloc_data(b), STRESS_ELEMENT, seed);
        if (i % 3u == 0u) { /* an extra reference that is dropped again at once */
            if (bloc_retain(b) != BLOC_OK) {
                w->status_errors++;
            }
            if (bloc_release(b) != BLOC_OK) {
                w->status_errors++;
            }
        }
        if (!ts_fill_matches((const uint8_t *)bloc_data(b), STRESS_ELEMENT, seed)) {
            w->mismatches++;
        }
        if (bloc_release(b) != BLOC_OK) {
            w->status_errors++;
        }
        w->done++;
    }
    return NULL;
}

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

void test_TS_04_stress(void)
{
    bloc_pool_t pool;
    static struct worker workers[STRESS_THREADS];
    bloc_handle_t held[STRESS_BLOCKS];
    unsigned i;

    ts_pool_setup(&pool, STRESS_BLOCKS, STRESS_ELEMENT);

    for (i = 0u; i < STRESS_THREADS; i++) {
        workers[i].pool = &pool;
        workers[i].id = i;
        TEST_ASSERT_EQUAL_INT(0,
                              pthread_create(&workers[i].thread, NULL, worker_main, &workers[i]));
    }
    for (i = 0u; i < STRESS_THREADS; i++) {
        TEST_ASSERT_EQUAL_INT(0, pthread_join(workers[i].thread, NULL));
    }

    for (i = 0u; i < STRESS_THREADS; i++) {
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, workers[i].alloc_failures, "allocation failed");
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, workers[i].status_errors, "operation failed");
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, workers[i].mismatches, "pattern mismatch");
        TEST_ASSERT_EQUAL_UINT(STRESS_ITERATIONS, (unsigned)workers[i].done);
    }

    /* nothing leaked, nothing duplicated: every block can be taken exactly once */
    TEST_ASSERT_EQUAL_UINT(STRESS_BLOCKS, bloc_pool_free_count(&pool));
    for (i = 0u; i < STRESS_BLOCKS; i++) {
        unsigned k;

        held[i] = bloc_alloc(&pool, 0u);
        TEST_ASSERT_NOT_NULL(held[i]);
        for (k = 0u; k < i; k++) {
            TEST_ASSERT_TRUE(held[k] != held[i]);
        }
    }
    TEST_ASSERT_NULL(bloc_alloc(&pool, 0u));
    for (i = 0u; i < STRESS_BLOCKS; i++) {
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(held[i]));
    }
    TEST_ASSERT_EQUAL_UINT(STRESS_BLOCKS, bloc_pool_free_count(&pool));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_deinit(&pool));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_TS_04_stress);
    return UNITY_END();
}

#else /* not the pthread configuration */

void setUp(void) {}

void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    return UNITY_END();
}

#endif
