/*
 * Debug check tests (tag DBG, implementation plan, section 8.10), the rows for the functions that
 * exist at this stage: every function that takes a handle (DBG-01..06), retain and release
 * (DBG-07), the overlap checks (DBG-08), the shared-mutation rows (DBG-09) and the release
 * invariant (DBG-10).
 *
 * The suite is compiled only in BLOC_DEBUG configurations.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bloc.h"
#include "ts_arena.h"
#include "ts_helpers.h"
#include "ts_snap.h"
#include "unity.h"

#if BLOC_DEBUG

#define SA ((size_t)BLOC_STORAGE_ALIGNMENT)

static ts_snap_t g_snap;

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

/* --- Table of the functions that take a handle (except retain and release) ------------------ */

/*
 * Every wrapper returns the function result as an integer. The bail value is 0 for the accessors
 * and BLOC_INVALID for the functions that return a status.
 */
static uintptr_t call_data(bloc_handle_t b) { return (uintptr_t)bloc_data(b); }
static uintptr_t call_len(bloc_handle_t b) { return (uintptr_t)bloc_len(b); }
static uintptr_t call_headroom(bloc_handle_t b) { return (uintptr_t)bloc_headroom(b); }
static uintptr_t call_tailroom(bloc_handle_t b) { return (uintptr_t)bloc_tailroom(b); }

static uintptr_t call_set_len(bloc_handle_t b) { return (uintptr_t)bloc_set_len(b, 0u); }
static uintptr_t call_add_header(bloc_handle_t b) { return (uintptr_t)bloc_add_header(b, 0u); }
static uintptr_t call_remove_header(bloc_handle_t b)
{
    return (uintptr_t)bloc_remove_header(b, 0u);
}

/*
 * The copy, append and prepend functions with n == 0. A two-handle function appears twice: once
 * with the handle under test as dst and once as src; the other role is filled with g_other, a
 * valid allocated buffer of the fixture.
 */
static bloc_handle_t g_other;
static uint8_t g_ext[8];
static uint8_t g_dst[16];

static uintptr_t call_copy_from(bloc_handle_t b) { return (uintptr_t)bloc_copy_from(b, g_ext, 0u); }
static uintptr_t call_copy_to(bloc_handle_t b) { return (uintptr_t)bloc_copy_to(b, g_ext, 0u, 0u); }
static uintptr_t call_copy_dst(bloc_handle_t b) { return (uintptr_t)bloc_copy(b, g_other); }
static uintptr_t call_copy_src(bloc_handle_t b) { return (uintptr_t)bloc_copy(g_other, b); }
static uintptr_t call_append_dst(bloc_handle_t b) { return (uintptr_t)bloc_append(b, g_other, 0u); }
static uintptr_t call_append_src(bloc_handle_t b) { return (uintptr_t)bloc_append(g_other, b, 0u); }
static uintptr_t call_append_data(bloc_handle_t b)
{
    return (uintptr_t)bloc_append_data(b, g_ext, 0u);
}
static uintptr_t call_prepend_dst(bloc_handle_t b)
{
    return (uintptr_t)bloc_prepend(b, g_other, 0u);
}
static uintptr_t call_prepend_src(bloc_handle_t b)
{
    return (uintptr_t)bloc_prepend(g_other, b, 0u);
}
static uintptr_t call_prepend_data(bloc_handle_t b)
{
    return (uintptr_t)bloc_prepend_data(b, g_ext, 0u);
}

static const struct {
    const char *name;
    uintptr_t (*call)(bloc_handle_t);
    uintptr_t bail; /* result when the handle is rejected */
    uintptr_t ok;   /* result for a valid handle (status functions) */
} g_functions[] = {
    {"bloc_data", call_data, 0u, 0u},
    {"bloc_len", call_len, 0u, 0u},
    {"bloc_headroom", call_headroom, 0u, 0u},
    {"bloc_tailroom", call_tailroom, 0u, 0u},
    {"bloc_set_len", call_set_len, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
    {"bloc_add_header", call_add_header, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
    {"bloc_remove_header", call_remove_header, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
    {"bloc_copy_from", call_copy_from, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
    {"bloc_copy_to", call_copy_to, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
    {"bloc_copy dst", call_copy_dst, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
    {"bloc_copy src", call_copy_src, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
    {"bloc_append dst", call_append_dst, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
    {"bloc_append src", call_append_src, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
    {"bloc_append_data", call_append_data, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
    {"bloc_prepend dst", call_prepend_dst, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
    {"bloc_prepend src", call_prepend_src, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
    {"bloc_prepend_data", call_prepend_data, (uintptr_t)BLOC_INVALID, (uintptr_t)BLOC_OK},
};

#define N_FUNCTIONS (sizeof(g_functions) / sizeof(g_functions[0]))

/* --- Defects ------------------------------------------------------------------------------ */

/*
 * A pool of two blocks with room for a fake handle inside block 0 and past the end. Block 0 is
 * the handle under test, block 1 stays free. The second handle that the two-handle functions need
 * (g_other) comes from a separate pool, so that a defect of the fixture pool never affects it.
 */
struct fixture {
    bloc_pool_t pool;
    uint8_t *storage;
    size_t size;
    bloc_handle_t real; /* block 0, allocated */
    bloc_pool_t other_pool;
    bloc_handle_t other; /* from other_pool, allocated */
};

static void fixture_setup(struct fixture *f)
{
    const bloc_size_t e = (bloc_size_t)(2u * SA + sizeof(struct bloc_handle) + 8u);
    const size_t stride = ts_expected_stride(e);

    f->size = 2u * stride;
    f->storage = ts_storage(f->size + stride, 0u); /* surplus for the one-past-the-end case */
    TEST_ASSERT_NOT_NULL(f->storage);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_pool_init(&f->pool, f->storage, f->size, 2u, e));
    f->real = bloc_alloc(&f->pool, 0u);
    ts_pool_setup(&f->other_pool, 1u, 16u);
    f->other = bloc_alloc(&f->other_pool, 0u);
    TEST_ASSERT_NOT_NULL(f->real);
    TEST_ASSERT_NOT_NULL(f->other);
    g_other = f->other;
}

static void fixture_release(struct fixture *f)
{
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(f->real));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(f->other));
}

static void make_fake(struct bloc_handle *at, bloc_pool_t *pool)
{
    memset(at, 0, sizeof(*at));
    at->refcount = 1u;
    at->link.pool = pool;
}

enum defect {
    DEF_OUTSIDE,     /* DBG-01: fake handle on the stack                       */
    DEF_UNALIGNED,   /* DBG-02: fake handle inside storage, off the stride grid */
    DEF_POOL_NULL,   /* DBG-03: real handle with link.pool == NULL              */
    DEF_POOL_UNINIT, /* DBG-04: real handle, pool marked uninitialized          */
    DEF_PAST_END,    /* DBG-05: handle at storage + element_count * stride      */
    DEF_FREE         /* DBG-06: free block                                      */
};

/* Applies a defect and returns the handle to pass; restore_defect() undoes it. */
static bloc_handle_t apply_defect(struct fixture *f, enum defect d, struct bloc_handle *stack_fake)
{
    switch (d) {
    case DEF_OUTSIDE:
        make_fake(stack_fake, &f->pool);
        return stack_fake;
    case DEF_UNALIGNED: {
        uint8_t *at = (uint8_t *)f->real + BLOC_ALIGN_UP(BLOC_HEADER_SIZE, BLOC_STORAGE_ALIGNMENT);
        struct bloc_handle *fake = (struct bloc_handle *)(void *)at;

        make_fake(fake, &f->pool);
        return fake;
    }
    case DEF_POOL_NULL:
        f->real->link.pool = NULL;
        return f->real;
    case DEF_POOL_UNINIT:
        f->pool.storage = NULL;
        return f->real;
    case DEF_PAST_END: {
        struct bloc_handle *fake =
            (struct bloc_handle *)(void *)(f->storage +
                                           (size_t)f->pool.element_count * f->pool.block_stride);

        make_fake(fake, &f->pool);
        return fake;
    }
    case DEF_FREE:
    default: {
        struct bloc_handle *free_block = ts_block(&f->pool, 1u);

        /*
         * Make the free block pass validity steps 2 to 5 (valid pool pointer, on the grid), so
         * that only step 1 (refcount != 0) can reject it.
         */
        free_block->link.pool = &f->pool;
        return free_block;
    }
    }
}

static void restore_defect(struct fixture *f, enum defect d)
{
    if (d == DEF_POOL_NULL) {
        f->real->link.pool = &f->pool;
    } else if (d == DEF_POOL_UNINIT) {
        f->pool.storage = f->storage;
    }
}

static void snap_fixture(const struct fixture *f)
{
    ts_snap_take(&g_snap, &f->pool, f->storage, f->size);
}

/* --- DBG-01..06 --------------------------------------------------------------------------- */

static void run_defect_table(enum defect d)
{
    size_t i;

    for (i = 0u; i < N_FUNCTIONS; i++) {
        struct fixture f;
        struct bloc_handle stack_fake;
        bloc_handle_t h;
        uintptr_t r = g_functions[i].bail + 1u;

        fixture_setup(&f);
        h = apply_defect(&f, d, &stack_fake);
        snap_fixture(&f);
        TS_EXPECT_ASSERT(r = g_functions[i].call(h));
        if (r != g_functions[i].bail) {
            TEST_FAIL_MESSAGE(g_functions[i].name);
        }
        TS_SNAP_CHECK(&g_snap);
        restore_defect(&f, d);
        fixture_release(&f);
    }
}

void test_DBG_01_fake_handle_outside_storage(void) { run_defect_table(DEF_OUTSIDE); }

void test_DBG_02_handle_off_stride_grid(void) { run_defect_table(DEF_UNALIGNED); }

void test_DBG_03_link_pool_null(void) { run_defect_table(DEF_POOL_NULL); }

void test_DBG_04_pool_uninitialized(void) { run_defect_table(DEF_POOL_UNINIT); }

void test_DBG_05_handle_one_past_end(void) { run_defect_table(DEF_PAST_END); }

void test_DBG_06_free_block(void) { run_defect_table(DEF_FREE); }

/* A valid handle must pass every check without an assertion (the control case of DBG-01..06). */
void test_DBG_valid_handle_passes(void)
{
    size_t i;

    for (i = 0u; i < N_FUNCTIONS; i++) {
        struct fixture f;
        uintptr_t r;

        fixture_setup(&f);
        TS_EXPECT_NO_ASSERT(r = g_functions[i].call(f.real));
        if (g_functions[i].bail != 0u) { /* status functions: BLOC_OK; accessors return data */
            TEST_ASSERT_EQUAL_UINT(g_functions[i].ok, r);
        }
        fixture_release(&f);
    }
}

/* --- DBG-07 ------------------------------------------------------------------------------- */

void test_DBG_07_retain_release_with_invalid_handles(void)
{
    enum defect d;

    for (d = DEF_OUTSIDE; d <= DEF_PAST_END; d++) {
        struct fixture f;
        struct bloc_handle stack_fake;
        bloc_handle_t h;
        bloc_status_t r = BLOC_OK;
        int k;

        for (k = 0; k < 2; k++) {
            fixture_setup(&f);
            h = apply_defect(&f, d, &stack_fake);
            snap_fixture(&f);
            if (k == 0) {
                TS_EXPECT_ASSERT(r = bloc_retain(h)); /* depth 0 at the assertion is checked */
            } else {
                TS_EXPECT_ASSERT(r = bloc_release(h));
            }
            TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
            TS_SNAP_CHECK(&g_snap);
            restore_defect(&f, d);
            fixture_release(&f);
        }
    }
}

/* --- DBG-08 ------------------------------------------------------------------------------- */

enum overlap_fn { OV_COPY_FROM, OV_APPEND_DATA, OV_PREPEND_DATA };

static bloc_status_t call_overlap(enum overlap_fn fn, bloc_handle_t b, const uint8_t *ext, size_t n)
{
    if (fn == OV_COPY_FROM) {
        return bloc_copy_from(b, ext, (bloc_size_t)n);
    }
    if (fn == OV_APPEND_DATA) {
        return bloc_append_data(b, ext, (bloc_size_t)n);
    }
    return bloc_prepend_data(b, ext, (bloc_size_t)n);
}

/*
 * One overlap case: the external range starts at write_start + delta (delta may be negative) and
 * is n bytes long. overlaps says whether the debug check must reject it.
 */
static void run_overlap_case(enum overlap_fn fn, long delta, size_t n, int overlaps)
{
    bloc_pool_t pool;
    bloc_handle_t b;
    const size_t e = ts_element_size_aligned();
    const uint8_t *write_start;
    bloc_status_t r = BLOC_OK;

    ts_pool_setup(&pool, 1u, (bloc_size_t)e);
    b = bloc_alloc(&pool, (bloc_size_t)(4u * BLOC_PAYLOAD_ALIGNMENT));
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 4u));
    ts_fill(ts_area(b), e, 0x33u);
    if (fn == OV_COPY_FROM) {
        write_start = (const uint8_t *)bloc_data(b);
    } else if (fn == OV_APPEND_DATA) {
        write_start = (const uint8_t *)bloc_data(b) + 4;
    } else {
        write_start = (const uint8_t *)bloc_data(b) - n;
    }
    ts_snap_take(&g_snap, &pool, pool.storage, (size_t)pool.block_stride);
    if (overlaps) {
        TS_EXPECT_ASSERT(r = call_overlap(fn, b, write_start + delta, n));
        TEST_ASSERT_EQUAL_INT(BLOC_INVALID, (int)r);
        TS_SNAP_CHECK(&g_snap); /* no byte written, no field changed */
    } else {
        TS_EXPECT_NO_ASSERT(r = call_overlap(fn, b, write_start + delta, n));
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
    }
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
}

void test_DBG_08_external_pointer_overlap(void)
{
    const size_t n = 2u;
    int fn;

    for (fn = OV_COPY_FROM; fn <= OV_PREPEND_DATA; fn++) {
        const enum overlap_fn f = (enum overlap_fn)fn;

        run_overlap_case(f, 0, n, 1);        /* identical range             */
        run_overlap_case(f, 1, n, 1);        /* starts inside the range     */
        run_overlap_case(f, -1, n, 1);       /* ends inside the range       */
        run_overlap_case(f, (long)n, n, 0);  /* starts where the write ends */
        run_overlap_case(f, -(long)n, n, 0); /* ends where the write starts */
        run_overlap_case(f, 1, 0u, 0);       /* n == 0, pointer inside      */
        run_overlap_case(f, 0, 0u, 0);
    }
}

/* --- DBG-09 ------------------------------------------------------------------------------- */

/* Every mutating function at refcount 2 asserts once, performs the operation and returns OK. */
void test_DBG_09_shared_mutation(void)
{
    bloc_pool_t pool;
    bloc_pool_t other_pool;
    bloc_handle_t b;
    bloc_handle_t o;
    const size_t e = ts_element_size_aligned();
    int k;

    ts_pool_setup(&pool, 2u, (bloc_size_t)e);
    ts_pool_setup(&other_pool, 1u, (bloc_size_t)e);
    o = bloc_alloc(&other_pool, 0u);
    TEST_ASSERT_NOT_NULL(o);
    ts_fill(ts_area(o), e, 0x01u);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(o, 3u));
    ts_fill(g_ext, sizeof(g_ext), 0x40u);
    b = bloc_alloc(&pool, (bloc_size_t)(4u * BLOC_PAYLOAD_ALIGNMENT));
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 8u));

    /* refcount == 1: no assertion. */
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 8u)));
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_remove_header(b, 1u)));
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_add_header(b, 1u)));
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy_from(b, g_ext, 4u)));
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy(b, o)));
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append(b, o, 1u)));
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_append_data(b, g_ext, 1u)));
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend(b, o, 1u)));
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_prepend_data(b, g_ext, 1u)));

    /* refcount == 2: one assertion, the operation is performed, the result is BLOC_OK. */
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_set_len(b, 8u));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_retain(b));
    for (k = 0; k < 9; k++) {
        bloc_status_t r = BLOC_INVALID;
        const size_t len = bloc_len(b);
        const size_t off = bloc_headroom(b);

        switch (k) {
        case 0:
            TS_EXPECT_ASSERT(r = bloc_set_len(b, 5u));
            TEST_ASSERT_EQUAL_UINT(5u, bloc_len(b));
            break;
        case 1:
            TS_EXPECT_ASSERT(r = bloc_remove_header(b, 2u));
            TEST_ASSERT_EQUAL_UINT(len - 2u, bloc_len(b));
            break;
        case 2:
            TS_EXPECT_ASSERT(r = bloc_add_header(b, 2u));
            TEST_ASSERT_EQUAL_UINT(len + 2u, bloc_len(b));
            break;
        case 3:
            TS_EXPECT_ASSERT(r = bloc_copy_from(b, g_ext, 4u));
            TEST_ASSERT_EQUAL_UINT(4u, bloc_len(b));
            break;
        case 4:
            TS_EXPECT_ASSERT(r = bloc_copy(b, o));
            TEST_ASSERT_EQUAL_UINT(3u, bloc_len(b));
            break;
        case 5:
            TS_EXPECT_ASSERT(r = bloc_append(b, o, 2u));
            TEST_ASSERT_EQUAL_UINT(len + 2u, bloc_len(b));
            break;
        case 6:
            TS_EXPECT_ASSERT(r = bloc_append_data(b, g_ext, 2u));
            TEST_ASSERT_EQUAL_UINT(len + 2u, bloc_len(b));
            break;
        case 7:
            TS_EXPECT_ASSERT(r = bloc_prepend(b, o, 2u));
            TEST_ASSERT_EQUAL_UINT(len + 2u, bloc_len(b));
            TEST_ASSERT_EQUAL_UINT(off - 2u, bloc_headroom(b));
            break;
        default:
            TS_EXPECT_ASSERT(r = bloc_prepend_data(b, g_ext, 2u));
            TEST_ASSERT_EQUAL_UINT(len + 2u, bloc_len(b));
            TEST_ASSERT_EQUAL_UINT(off - 2u, bloc_headroom(b));
            break;
        }
        TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r);
    }
    /* Read-only operations never assert on a shared buffer. */
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy_to(b, g_dst, 1u, 0u)));
    TS_EXPECT_NO_ASSERT(TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_copy(o, b)));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)bloc_release(o));
}

/* --- DBG-10 ------------------------------------------------------------------------------- */

void test_DBG_10_release_invariant(void)
{
    bloc_pool_t pool;
    bloc_handle_t b;
    bloc_status_t r = BLOC_OK;

    ts_pool_setup(&pool, 4u, 16u);
    b = bloc_alloc(&pool, 0u);
    TEST_ASSERT_NOT_NULL(b);

    /* After the final release, active_count would still exceed element_count. */
    pool.active_count = (bloc_count_t)(pool.element_count + 2u);
    TS_EXPECT_ASSERT(r = bloc_release(b));
    TEST_ASSERT_EQUAL_INT(BLOC_OK, (int)r); /* continue semantics: the release completes */
    TEST_ASSERT_EQUAL_UINT(0u, b->refcount);
    TEST_ASSERT_EQUAL_PTR(b, pool.free_head);

    pool.active_count = 0u; /* restore */
    TEST_ASSERT_EQUAL_UINT(4u, bloc_pool_free_count(&pool));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_DBG_01_fake_handle_outside_storage);
    RUN_TEST(test_DBG_02_handle_off_stride_grid);
    RUN_TEST(test_DBG_03_link_pool_null);
    RUN_TEST(test_DBG_04_pool_uninitialized);
    RUN_TEST(test_DBG_05_handle_one_past_end);
    RUN_TEST(test_DBG_06_free_block);
    RUN_TEST(test_DBG_valid_handle_passes);
    RUN_TEST(test_DBG_07_retain_release_with_invalid_handles);
    RUN_TEST(test_DBG_08_external_pointer_overlap);
    RUN_TEST(test_DBG_09_shared_mutation);
    RUN_TEST(test_DBG_10_release_invariant);
    return UNITY_END();
}

#else /* !BLOC_DEBUG */

void setUp(void) {}

void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();
    return UNITY_END();
}

#endif
