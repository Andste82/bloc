/*
 * Model-based test (MODEL-01, MODEL-02; implementation plan section 8.12, tag ALL).
 *
 * A reference model made of plain arrays (per buffer: offset, length, refcount, allocated flag
 * and the bytes of the whole data area) is driven in lockstep with BLOC by a seeded
 * ts_xorshift32 sequence of random operations over two pools. Every public operation takes part
 * and is checked for the status it returns. After every step the state of every live buffer, the
 * accessors, the free counts and the statistics are compared with the model, and the guard
 * bands of the storage are verified.
 *
 * The lengths that the operations get are valid about 70 % of the time. Where the configuration
 * does not check parameters (BLOC_CHECKS == 0) only valid parameters are generated, because the
 * behaviour for bad ones is undefined there. In debug configurations the model also predicts
 * the assertions: one for every rejected parameter and one for every mutation of a shared
 * buffer.
 *
 * A mismatch is reported with the seed, the step and the operation.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bloc/bloc.h"
#include "ts_arena.h"
#include "ts_helpers.h"
#include "unity.h"

#define N_POOLS 2u
#define N_STEPS 20000u
#define MAX_ELEMENT 1024u
#define MAX_BLOCKS 6u
#define N_SLOTS (N_POOLS * MAX_BLOCKS)

/* Percentage of the generated lengths that are valid. */
#define VALID_PERCENT 70u

static const uint32_t g_seeds[] = {0x1badb002u, 0x2545f491u, 0xdeadbeefu};

enum op {
    OP_ALLOC,
    OP_CALLOC,
    OP_RETAIN,
    OP_RELEASE,
    OP_SET_LEN,
    OP_ADD_HEADER,
    OP_REMOVE_HEADER,
    OP_COPY_FROM,
    OP_COPY_TO,
    OP_COPY,
    OP_APPEND,
    OP_APPEND_DATA,
    OP_PREPEND,
    OP_PREPEND_DATA,
    OP_COUNT
};

static const char *const g_op_names[OP_COUNT] = {
    "alloc",     "calloc",  "retain", "release", "set_len",     "add_header", "remove_header",
    "copy_from", "copy_to", "copy",   "append",  "append_data", "prepend",    "prepend_data"};

/* Operation mix in percent, in the order of enum op. */
static const unsigned g_weights[OP_COUNT] = {12u, 6u, 8u, 14u, 10u, 6u, 6u,
                                             6u,  6u, 6u, 6u,  6u,  4u, 4u};

/* One block of one pool in the model. */
struct mbuf {
    int live;
    unsigned refcount;
    size_t offset;
    size_t len;
    uint8_t area[MAX_ELEMENT]; /* the whole data area, headroom included */
};

struct mpool {
    bloc_pool_t pool;
    size_t element_size;
    size_t blocks;
    size_t active;
    size_t high_water;
    size_t failures;
};

static struct mbuf g_buf[N_SLOTS];
static struct mpool g_mp[N_POOLS];
static uint8_t g_ext[MAX_ELEMENT];
static uint8_t g_out[MAX_ELEMENT];
static uint32_t g_rng;
static uint32_t g_seed;
static unsigned g_step;
static enum op g_op;

void setUp(void) { ts_test_setup(); }

void tearDown(void) { ts_test_teardown(); }

/* --- Failure reporting -------------------------------------------------------------------- */

static void fail(const char *what)
{
    static char msg[160];

    (void)snprintf(msg, sizeof(msg), "MODEL mismatch: seed 0x%08lx step %u op %s: %s",
                   (unsigned long)g_seed, g_step, g_op_names[g_op], what);
    TEST_FAIL_MESSAGE(msg);
}

static void expect(int condition, const char *what)
{
    if (!condition) {
        fail(what);
    }
}

/* --- Random numbers ----------------------------------------------------------------------- */

static unsigned rnd(unsigned n) { return (unsigned)(ts_xorshift32(&g_rng) % n); }

/*
 * A length for an operation whose valid values are 0..max. About VALID_PERCENT percent of the
 * results are valid, the others are above max (also BLOC_SIZE_MAX, but only where bad parameters
 * are checked). Sets *valid.
 */
static size_t gen_len(size_t max, int *valid)
{
    if (!BLOC_CHECKS || rnd(100u) < VALID_PERCENT) {
        *valid = 1;
        return (size_t)rnd((unsigned)max + 1u);
    }
    *valid = 0;
    if (rnd(8u) == 0u) {
        return (size_t)BLOC_SIZE_MAX;
    }
    return max + 1u + (size_t)rnd(6u);
}

/* --- Model helpers ------------------------------------------------------------------------ */

static size_t slot_of(size_t pool, size_t block) { return pool * MAX_BLOCKS + block; }

static struct mbuf *mbuf_of(size_t slot) { return &g_buf[slot]; }

static size_t pool_of(size_t slot) { return slot / MAX_BLOCKS; }

static bloc_handle_t handle_of(size_t slot)
{
    return ts_block(&g_mp[pool_of(slot)].pool, slot % MAX_BLOCKS);
}

static size_t esize(size_t slot) { return g_mp[pool_of(slot)].element_size; }

static const uint8_t *payload_of(size_t slot)
{
    return mbuf_of(slot)->area + mbuf_of(slot)->offset;
}

/* Index of a random live slot, or N_SLOTS if there is none. */
static size_t pick_live(void)
{
    size_t live[N_SLOTS];
    size_t n = 0u;
    size_t i;

    for (i = 0u; i < N_SLOTS; i++) {
        if (g_buf[i].live) {
            live[n++] = i;
        }
    }
    return n == 0u ? N_SLOTS : live[rnd((unsigned)n)];
}

static void fill_random(uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0u; i < n; i++) {
        p[i] = (uint8_t)ts_xorshift32(&g_rng);
    }
}

static void setup_run(uint32_t seed)
{
    size_t p;
    size_t i;

    memset(g_buf, 0, sizeof(g_buf));
    memset(g_mp, 0, sizeof(g_mp));
    g_seed = seed;
    g_rng = seed;
    g_step = 0u;
    g_op = OP_ALLOC;
    for (p = 0u; p < N_POOLS; p++) {
        g_mp[p].blocks = p == 0u ? 5u : 3u;
        g_mp[p].element_size = ts_element_size_aligned() + p * 3u * (size_t)BLOC_PAYLOAD_ALIGNMENT;
        TEST_ASSERT_TRUE(g_mp[p].element_size <= MAX_ELEMENT);
        TEST_ASSERT_TRUE(g_mp[p].blocks <= MAX_BLOCKS);
        ts_pool_setup(&g_mp[p].pool, (bloc_count_t)g_mp[p].blocks,
                      (bloc_size_t)g_mp[p].element_size);
    }
    for (i = 0u; i < N_SLOTS; i++) {
        g_buf[i].live = 0;
    }
    fill_random(g_ext, sizeof(g_ext));
}

/* --- Comparison with the model ------------------------------------------------------------ */

static void compare_slot(size_t slot, int whole_area)
{
    const struct mbuf *m = mbuf_of(slot);
    const bloc_handle_t h = handle_of(slot);

    expect(m->live != 0, "slot not live");
    expect((size_t)h->offset == m->offset, "offset differs");
    expect((size_t)h->len == m->len, "len differs");
    expect((unsigned)h->refcount == m->refcount, "refcount differs");
    expect(h->link.pool == &g_mp[pool_of(slot)].pool, "owning pool differs");
    expect((size_t)bloc_headroom(h) == m->offset, "headroom differs");
    expect((size_t)bloc_len(h) == m->len, "bloc_len differs");
    expect((size_t)bloc_tailroom(h) == esize(slot) - m->offset - m->len, "tailroom differs");
    expect((const uint8_t *)bloc_data(h) == ts_area(h) + m->offset, "data pointer differs");
    expect(memcmp(bloc_data(h), payload_of(slot), m->len) == 0, "payload bytes differ");
    if (whole_area) {
        expect(memcmp(ts_area(h), m->area, esize(slot)) == 0, "data area differs");
    }
}

static void compare_all(int whole_area)
{
    size_t p;
    size_t i;

    for (p = 0u; p < N_POOLS; p++) {
        const struct mpool *mp = &g_mp[p];

        expect((size_t)bloc_pool_free_count(&mp->pool) == mp->blocks - mp->active,
               "free_count differs");
        expect((size_t)mp->pool.active_count == mp->active, "active_count differs");
#if BLOC_STATS
        {
            bloc_pool_stats_t st;

            expect(bloc_pool_get_stats(&mp->pool, &st) == BLOC_OK, "get_stats failed");
            expect((size_t)st.high_water == mp->high_water, "high_water differs");
            expect((size_t)st.alloc_failures == mp->failures, "alloc_failures differs");
        }
#endif
    }
    for (i = 0u; i < N_SLOTS; i++) {
        if (g_buf[i].live) {
            compare_slot(i, whole_area);
        } else if (g_mp[pool_of(i)].blocks > i % MAX_BLOCKS) {
            expect(handle_of(i)->refcount == 0u, "free block has a refcount");
        }
    }
    expect(ts_arena_guards_ok() != 0, "guard band damaged");
}

/* --- Operations --------------------------------------------------------------------------- */

/*
 * How many assertions the call is expected to raise: one for a rejected parameter, else one for
 * a mutation of a shared buffer (debug builds only).
 */
static unsigned expected_asserts(int valid, int mutates, const struct mbuf *dst)
{
    if (!BLOC_DEBUG) {
        return 0u;
    }
    if (!valid) {
        return 1u;
    }
    return mutates && dst->refcount > 1u ? 1u : 0u;
}

#define CALL(expected_assert_count, result, call)                                                  \
    do {                                                                                           \
        if ((expected_assert_count) != 0u) {                                                       \
            TS_EXPECT_ASSERT(result = (call));                                                     \
        } else {                                                                                   \
            TS_EXPECT_NO_ASSERT(result = (call));                                                  \
        }                                                                                          \
    } while (0)

static void check_status(bloc_status_t got, bloc_status_t want)
{
    expect(got == want, "status differs");
}

static void op_alloc(int zero)
{
    const size_t p = rnd(N_POOLS);
    struct mpool *mp = &g_mp[p];
    const size_t max_off = mp->element_size - mp->element_size % (size_t)BLOC_PAYLOAD_ALIGNMENT;
    int valid;
    const size_t headroom = gen_len(max_off, &valid);
    bloc_handle_t h;
    size_t block;
    size_t slot;
    struct mbuf *m;

    /* An oversize headroom is a valid runtime condition (no assertion, no failure counted). */
    TS_EXPECT_NO_ASSERT(h = zero ? bloc_calloc(&mp->pool, (bloc_size_t)headroom)
                                 : bloc_alloc(&mp->pool, (bloc_size_t)headroom));
    if (!valid) {
        expect(h == NULL, "oversize headroom was accepted");
        return;
    }
    if (mp->active == mp->blocks) {
        expect(h == NULL, "alloc on an empty pool succeeded");
        if (mp->failures < (size_t)BLOC_COUNT_MAX) {
            mp->failures++;
        }
        return;
    }
    expect(h != NULL, "alloc failed although a block is free");
    for (block = 0u; block < mp->blocks; block++) {
        if (ts_block(&mp->pool, block) == h) {
            break;
        }
    }
    expect(block < mp->blocks, "alloc returned a block outside the pool");
    slot = slot_of(p, block);
    m = mbuf_of(slot);
    expect(!m->live, "alloc returned a block that is in use");
    m->live = 1;
    m->refcount = 1u;
    m->len = 0u;
    m->offset = ts_round_up_pa(headroom);
    mp->active++;
    if (mp->active > mp->high_water) {
        mp->high_water = mp->active;
    }
    if (zero) {
        memset(m->area, 0, mp->element_size);
    } else {
        /* The contents are undefined: give both sides the same known bytes. */
        fill_random(m->area, mp->element_size);
        memcpy(ts_area(h), m->area, mp->element_size);
    }
    compare_slot(slot, 1);
}

static void op_retain(void)
{
    const size_t s = pick_live();
    bloc_status_t r = BLOC_INVALID;

    if (s == N_SLOTS) {
        return;
    }
    TS_EXPECT_NO_ASSERT(r = bloc_retain(handle_of(s)));
    if (mbuf_of(s)->refcount == (unsigned)BLOC_REFCOUNT_MAX) {
        check_status(r, BLOC_OVERFLOW);
    } else {
        check_status(r, BLOC_OK);
        mbuf_of(s)->refcount++;
    }
}

static void op_release(void)
{
    const size_t s = pick_live();
    struct mbuf *m;
    bloc_status_t r = BLOC_INVALID;

    if (s == N_SLOTS) {
        return;
    }
    m = mbuf_of(s);
    TS_EXPECT_NO_ASSERT(r = bloc_release(handle_of(s)));
    check_status(r, BLOC_OK);
    m->refcount--;
    if (m->refcount == 0u) {
        m->live = 0;
        g_mp[pool_of(s)].active--;
    }
}

static void op_set_len(void)
{
    const size_t s = pick_live();
    struct mbuf *m;
    int valid;
    size_t len;
    bloc_status_t r = BLOC_INVALID;
    unsigned a;

    if (s == N_SLOTS) {
        return;
    }
    m = mbuf_of(s);
    len = gen_len(esize(s) - m->offset, &valid);
    a = expected_asserts(valid, 1, m);
    CALL(a, r, bloc_set_len(handle_of(s), (bloc_size_t)len));
    check_status(r, valid ? BLOC_OK : BLOC_BOUNDS);
    if (valid) {
        m->len = len;
    }
}

static void op_add_header(void)
{
    const size_t s = pick_live();
    struct mbuf *m;
    int valid;
    size_t n;
    bloc_status_t r = BLOC_INVALID;
    unsigned a;

    if (s == N_SLOTS) {
        return;
    }
    m = mbuf_of(s);
    n = gen_len(m->offset, &valid);
    a = expected_asserts(valid, 1, m);
    CALL(a, r, bloc_add_header(handle_of(s), (bloc_size_t)n));
    check_status(r, valid ? BLOC_OK : BLOC_BOUNDS);
    if (valid) {
        m->offset -= n;
        m->len += n;
    }
}

static void op_remove_header(void)
{
    const size_t s = pick_live();
    struct mbuf *m;
    int valid;
    size_t n;
    bloc_status_t r = BLOC_INVALID;
    unsigned a;

    if (s == N_SLOTS) {
        return;
    }
    m = mbuf_of(s);
    n = gen_len(m->len, &valid);
    a = expected_asserts(valid, 1, m);
    CALL(a, r, bloc_remove_header(handle_of(s), (bloc_size_t)n));
    check_status(r, valid ? BLOC_OK : BLOC_BOUNDS);
    if (valid) {
        m->offset += n;
        m->len -= n;
    }
}

static void op_copy_from(void)
{
    const size_t s = pick_live();
    struct mbuf *m;
    int valid;
    size_t n;
    bloc_status_t r = BLOC_INVALID;
    unsigned a;

    if (s == N_SLOTS) {
        return;
    }
    m = mbuf_of(s);
    n = gen_len(esize(s) - m->offset, &valid);
    a = expected_asserts(valid, 1, m);
    fill_random(g_ext, 16u); /* fresh bytes at the start of the external buffer */
    CALL(a, r, bloc_copy_from(handle_of(s), g_ext, (bloc_size_t)n));
    check_status(r, valid ? BLOC_OK : BLOC_BOUNDS);
    if (valid) {
        memcpy(m->area + m->offset, g_ext, n);
        m->len = n;
    }
}

static void op_copy_to(void)
{
    const size_t s = pick_live();
    struct mbuf *m;
    int valid;
    size_t pos;
    size_t n;
    bloc_status_t r = BLOC_INVALID;
    unsigned a;

    if (s == N_SLOTS) {
        return;
    }
    m = mbuf_of(s);
    if (!BLOC_CHECKS || rnd(100u) < VALID_PERCENT) {
        valid = 1;
        pos = rnd((unsigned)m->len + 1u);
        n = gen_len(m->len - pos, &valid);
    } else {
        valid = 0;
        if (rnd(2u) == 0u) {
            pos = m->len + 1u + rnd(4u);
            n = rnd(4u);
        } else {
            pos = rnd((unsigned)m->len + 1u);
            n = m->len - pos + 1u + rnd(4u);
        }
    }
    a = expected_asserts(valid, 0, m);
    memset(g_out, 0xEE, sizeof(g_out));
    CALL(a, r, bloc_copy_to(handle_of(s), g_out, (bloc_size_t)n, (bloc_size_t)pos));
    check_status(r, valid ? BLOC_OK : BLOC_BOUNDS);
    if (valid) {
        size_t i;

        expect(memcmp(g_out, payload_of(s) + pos, n) == 0, "copy_to output differs");
        for (i = n; i < sizeof(g_out); i++) {
            if (g_out[i] != 0xEEu) {
                fail("copy_to wrote past n");
            }
        }
    }
}

static void op_copy(void)
{
    const size_t d = pick_live();
    const size_t s = pick_live();
    struct mbuf *md;
    const struct mbuf *ms;
    int valid;
    bloc_status_t r = BLOC_INVALID;
    unsigned a;

    if (d == N_SLOTS) {
        return;
    }
    md = mbuf_of(d);
    ms = mbuf_of(s);
    /* copy(b, b) is a no-op that is always valid; otherwise the payload must fit. */
    valid = d == s || ms->len <= esize(d) - md->offset;
    if (!BLOC_CHECKS && !valid) {
        return;
    }
    a = expected_asserts(valid, d != s, md);
    CALL(a, r, bloc_copy(handle_of(d), handle_of(s)));
    check_status(r, valid ? BLOC_OK : BLOC_BOUNDS);
    if (valid && d != s) {
        memcpy(md->area + md->offset, ms->area + ms->offset, ms->len);
        md->len = ms->len;
    }
}

static void op_append(int from_buffer)
{
    const size_t d = pick_live();
    const size_t s = from_buffer ? pick_live() : N_SLOTS;
    struct mbuf *md;
    int valid;
    size_t tail;
    size_t max;
    size_t n;
    bloc_status_t r = BLOC_INVALID;
    unsigned a;

    if (d == N_SLOTS) {
        return;
    }
    md = mbuf_of(d);
    tail = esize(d) - md->offset - md->len;
    max = from_buffer && mbuf_of(s)->len < tail ? mbuf_of(s)->len : tail;
    n = gen_len(max, &valid);
    a = expected_asserts(valid, 1, md);
    if (from_buffer) {
        CALL(a, r, bloc_append(handle_of(d), handle_of(s), (bloc_size_t)n));
    } else {
        fill_random(g_ext, 16u);
        CALL(a, r, bloc_append_data(handle_of(d), g_ext, (bloc_size_t)n));
    }
    check_status(r, valid ? BLOC_OK : BLOC_BOUNDS);
    if (valid) {
        /* For a self-append the source range [offset, offset + n) and the destination range are
         * disjoint, so a plain memcpy in the model is exact. */
        memcpy(md->area + md->offset + md->len,
               from_buffer ? mbuf_of(s)->area + mbuf_of(s)->offset : g_ext, n);
        md->len += n;
    }
}

static void op_prepend(int from_buffer)
{
    const size_t d = pick_live();
    const size_t s = from_buffer ? pick_live() : N_SLOTS;
    struct mbuf *md;
    int valid;
    size_t max;
    size_t n;
    bloc_status_t r = BLOC_INVALID;
    unsigned a;

    if (d == N_SLOTS) {
        return;
    }
    md = mbuf_of(d);
    max = from_buffer && mbuf_of(s)->len < md->offset ? mbuf_of(s)->len : md->offset;
    n = gen_len(max, &valid);
    a = expected_asserts(valid, 1, md);
    if (from_buffer) {
        CALL(a, r, bloc_prepend(handle_of(d), handle_of(s), (bloc_size_t)n));
    } else {
        fill_random(g_ext, 16u);
        CALL(a, r, bloc_prepend_data(handle_of(d), g_ext, (bloc_size_t)n));
    }
    check_status(r, valid ? BLOC_OK : BLOC_BOUNDS);
    if (valid) {
        memcpy(md->area + md->offset - n,
               from_buffer ? mbuf_of(s)->area + mbuf_of(s)->offset : g_ext, n);
        md->offset -= n;
        md->len += n;
    }
}

static enum op pick_op(void)
{
    unsigned r = rnd(100u);
    unsigned acc = 0u;
    unsigned i;

    for (i = 0u; i < (unsigned)OP_COUNT; i++) {
        acc += g_weights[i];
        if (r < acc) {
            return (enum op)i;
        }
    }
    return OP_RELEASE;
}

/* Picks and runs one random operation. */
static void run_step(void)
{
    g_op = pick_op();
    switch (g_op) {
    case OP_ALLOC:
        op_alloc(0);
        break;
    case OP_CALLOC:
        op_alloc(1);
        break;
    case OP_RETAIN:
        op_retain();
        break;
    case OP_RELEASE:
        op_release();
        break;
    case OP_SET_LEN:
        op_set_len();
        break;
    case OP_ADD_HEADER:
        op_add_header();
        break;
    case OP_REMOVE_HEADER:
        op_remove_header();
        break;
    case OP_COPY_FROM:
        op_copy_from();
        break;
    case OP_COPY_TO:
        op_copy_to();
        break;
    case OP_COPY:
        op_copy();
        break;
    case OP_APPEND:
        op_append(1);
        break;
    case OP_APPEND_DATA:
        op_append(0);
        break;
    case OP_PREPEND:
        op_prepend(1);
        break;
    default:
        op_prepend(0);
        break;
    }
}

/* --- Runs --------------------------------------------------------------------------------- */

static unsigned g_counts[OP_COUNT];

static void run_random_steps(uint32_t seed)
{
    setup_run(seed);
    memset(g_counts, 0, sizeof(g_counts));
    for (g_step = 1u; g_step <= N_STEPS; g_step++) {
        run_step();
        g_counts[g_op]++;
        /* Every live payload and every accessor after every step, the whole data areas every
         * 32nd step (a write outside the payload would show there). */
        compare_all(g_step % 32u == 0u);
    }
    g_step = N_STEPS;
    compare_all(1);
}

/* MODEL-02: every buffer goes back, every pool returns to full and can be deinitialized. */
static void release_everything(void)
{
    size_t s;
    size_t p;

    g_op = OP_RELEASE;
    for (s = 0u; s < N_SLOTS; s++) {
        while (g_buf[s].live) {
            const bloc_status_t r = bloc_release(handle_of(s));

            check_status(r, BLOC_OK);
            g_buf[s].refcount--;
            if (g_buf[s].refcount == 0u) {
                g_buf[s].live = 0;
                g_mp[pool_of(s)].active--;
            }
            compare_all(0);
        }
    }
    for (p = 0u; p < N_POOLS; p++) {
        expect(bloc_pool_free_count(&g_mp[p].pool) == (bloc_count_t)g_mp[p].blocks,
               "pool is not full after releasing everything");
        expect(bloc_pool_deinit(&g_mp[p].pool) == BLOC_OK, "deinit failed");
    }
}

/* All operations must have been exercised, with successes and failures. */
static void check_coverage_of_the_mix(void)
{
    unsigned i;

    for (i = 0u; i < (unsigned)OP_COUNT; i++) {
        if (g_counts[i] < 100u) {
            g_op = (enum op)i;
            fail("operation was hardly exercised");
        }
    }
}

void test_MODEL_01_lockstep_with_reference_model(void)
{
    size_t i;

    for (i = 0u; i < sizeof(g_seeds) / sizeof(g_seeds[0]); i++) {
        run_random_steps(g_seeds[i]);
        check_coverage_of_the_mix();
        release_everything();
        ts_arena_reset();
    }
}

void test_MODEL_02_release_all_returns_pools_to_full(void)
{
    run_random_steps(0x600df00du);
    release_everything();
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)g_mp[0].active);
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)g_mp[1].active);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_MODEL_01_lockstep_with_reference_model);
    RUN_TEST(test_MODEL_02_release_all_returns_pools_to_full);
    return UNITY_END();
}
