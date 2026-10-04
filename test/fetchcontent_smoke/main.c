/*
 * BLOC FetchContent smoke test (implementation plan, section 9.8).
 *
 * Uses only the public API and returns a distinct non-zero code for each failed check. There is no
 * stdio, so the program also links bare-metal.
 *
 * Check 2 is the ODR detector: the layout values below are computed by the consumer's copy of the
 * layout macros and compared with what the library did, so they only agree if the library and the
 * consumer were compiled with the same BLOC configuration (CM-06, FC-04).
 */
#include <stddef.h>
#include <stdint.h>

#include "bloc/bloc.h"

#define SMOKE_COUNT 4
#define SMOKE_ELEMENT 64
#define SMOKE_HEADROOM 8

_Static_assert(BLOC_POOL_SIZE(SMOKE_COUNT, SMOKE_ELEMENT) > 0, "the pool size must be positive");

static BLOC_POOL_STORAGE(g_storage, SMOKE_COUNT, SMOKE_ELEMENT);
static bloc_pool_t g_pool;

int main(void)
{
    bloc_handle_t a;
    bloc_handle_t b;
    size_t stride;
    size_t data_offset;
    uint8_t out[8];
    static const uint8_t payload[4] = {0x11u, 0x22u, 0x33u, 0x44u};
    static const uint8_t header[4] = {0xA1u, 0xA2u, 0xA3u, 0xA4u};
#if BLOC_STATS && !defined(SMOKE_MISMATCH)
    bloc_pool_stats_t stats;
#endif

    /* 1. Pool initialization. */
    if (bloc_pool_init(&g_pool, g_storage, sizeof(g_storage), SMOKE_COUNT, SMOKE_ELEMENT) !=
        BLOC_OK) {
        return 10;
    }

    /* 2. Layout cross-check, before anything else that depends on the layout. */
    a = bloc_alloc(&g_pool, SMOKE_HEADROOM);
    b = bloc_alloc(&g_pool, SMOKE_HEADROOM);
    if (a == NULL || b == NULL) {
        return 11;
    }
    if ((uint8_t *)a != g_storage) {
        return 20;
    }
    stride = (size_t)((uint8_t *)b - (uint8_t *)a);
    if (stride != BLOC_BLOCK_STRIDE(SMOKE_ELEMENT)) {
        return 21;
    }
    /* The effective headroom is the request rounded up to the payload alignment. */
    data_offset = BLOC_HEADER_SIZE + BLOC_ALIGN_UP(SMOKE_HEADROOM, BLOC_PAYLOAD_ALIGNMENT);
    if ((size_t)((uint8_t *)bloc_data(a) - (uint8_t *)a) != data_offset) {
        return 22;
    }
    if ((size_t)bloc_headroom(a) != BLOC_ALIGN_UP(SMOKE_HEADROOM, BLOC_PAYLOAD_ALIGNMENT)) {
        return 23;
    }
    if (bloc_len(a) != 0u || bloc_tailroom(a) + bloc_headroom(a) != SMOKE_ELEMENT) {
        return 24;
    }

    /* 3. Append a payload, add a header in the headroom, read both back. */
    if (bloc_append_data(a, payload, sizeof(payload)) != BLOC_OK) {
        return 30;
    }
    if (bloc_add_header(a, sizeof(header)) != BLOC_OK) {
        return 31;
    }
    {
        uint8_t *front = (uint8_t *)bloc_data(a);
        size_t i;

        for (i = 0u; i < sizeof(header); i++) {
            front[i] = header[i];
        }
    }
    if (bloc_len(a) != sizeof(header) + sizeof(payload)) {
        return 32;
    }
    if (bloc_copy_to(a, out, sizeof(header), 0u) != BLOC_OK) {
        return 33;
    }
    if (out[0] != header[0] || out[1] != header[1] || out[2] != header[2] || out[3] != header[3]) {
        return 34;
    }
    if (bloc_copy_to(a, out, sizeof(payload), sizeof(header)) != BLOC_OK) {
        return 35;
    }
    if (out[0] != payload[0] || out[1] != payload[1] || out[2] != payload[2] ||
        out[3] != payload[3]) {
        return 36;
    }

    /* 4. Statistics. The deliberately mismatched build (FC-04) has no such function in the
     * default-configured library, and check 2 has failed by now anyway. */
#if BLOC_STATS && !defined(SMOKE_MISMATCH)
    if (bloc_pool_get_stats(&g_pool, &stats) != BLOC_OK) {
        return 40;
    }
    if (stats.high_water != 2u || stats.alloc_failures != 0u) {
        return 41;
    }
#endif

    /* 5. Release and shut down. */
    if (bloc_release(a) != BLOC_OK || bloc_release(b) != BLOC_OK) {
        return 50;
    }
    if (bloc_pool_free_count(&g_pool) != SMOKE_COUNT) {
        return 51;
    }
    if (bloc_pool_deinit(&g_pool) != BLOC_OK) {
        return 52;
    }
    return 0;
}
