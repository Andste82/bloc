/*
 * BLOC example: build a small packet with headroom, hand it to a second owner and release it.
 *
 * The program uses only the public API of bloc.h and works with every project configuration.
 * It returns 0 on success and 1 as soon as a check fails.
 */
#include <stdio.h>
#include <string.h>

#include "bloc/bloc.h"

#define PACKET_COUNT 4u
#define PACKET_SIZE 64u
#define HEADROOM 8u
#define HEADER_SIZE 4u

/* Static, correctly aligned storage for PACKET_COUNT buffers of PACKET_SIZE data bytes. */
static BLOC_POOL_STORAGE(storage, PACKET_COUNT, PACKET_SIZE);
static bloc_pool_t pool;

static int check(bool condition, const char *what)
{
    if (!condition) {
        (void)fprintf(stderr, "example failed: %s\n", what);
        return 1;
    }
    return 0;
}

int main(void)
{
    static const char payload[] = "hello, bloc";
    static const uint8_t header[HEADER_SIZE] = {0xB1, 0x0C, 0x00, 0x01};
    char out[sizeof(payload)];
    bloc_handle_t b;
    bloc_handle_t c;
    int failed = 0;

    /* 1. Initialize the pool over the static storage. */
    failed |=
        check(bloc_pool_init(&pool, storage, sizeof(storage), PACKET_COUNT, PACKET_SIZE) == BLOC_OK,
              "pool init");
    failed |= check(bloc_pool_free_count(&pool) == PACKET_COUNT, "all blocks free");
    if (failed) {
        return 1;
    }

    /* 2. Allocate a buffer and keep headroom for a header that is added later. */
    b = bloc_alloc(&pool, HEADROOM);
    failed |= check(b != NULL, "alloc");
    if (b == NULL) {
        return 1;
    }
    failed |= check(bloc_headroom(b) >= HEADROOM, "headroom reserved");
    failed |= check(bloc_len(b) == 0u, "new buffer is empty");

    /* 3. Append the payload behind the (empty) payload window. */
    failed |= check(bloc_append_data(b, payload, (bloc_size_t)sizeof(payload)) == BLOC_OK,
                    "append payload");

    /* 4. Expose the headroom as payload without copying and write the header in place. */
    failed |= check(bloc_add_header(b, HEADER_SIZE) == BLOC_OK, "add header");
    (void)memcpy(bloc_data(b), header, sizeof(header));
    failed |= check(bloc_len(b) == HEADER_SIZE + sizeof(payload), "length after add_header");

    /* 5. A second owner shares the buffer; the block returns to the pool after the last release. */
    failed |= check(bloc_retain(b) == BLOC_OK, "retain");
    failed |= check(bloc_release(b) == BLOC_OK, "first release");
    failed |= check(bloc_pool_free_count(&pool) == PACKET_COUNT - 1u, "block still in use");

    /* 6. The receiver strips the header again and reads the payload. */
    failed |= check(bloc_remove_header(b, HEADER_SIZE) == BLOC_OK, "remove header");
    failed |= check(bloc_copy_to(b, out, (bloc_size_t)sizeof(out), 0u) == BLOC_OK, "copy out");
    failed |= check(memcmp(out, payload, sizeof(payload)) == 0, "payload intact");
    (void)printf("payload: %s\n", out);

    /* 7. A second buffer is built back to front: payload first, then two prepended prefixes. */
    c = bloc_alloc(&pool, HEADROOM);
    failed |= check(c != NULL, "alloc second buffer");
    if (c == NULL) {
        return 1;
    }
    failed |= check(bloc_append_data(c, payload, (bloc_size_t)sizeof(payload)) == BLOC_OK,
                    "append payload to second buffer");
    failed |= check(bloc_prepend_data(c, header, HEADER_SIZE) == BLOC_OK, "prepend header bytes");
    failed |=
        check(bloc_prepend(c, b, HEADER_SIZE) == BLOC_OK, "prepend bytes of the first buffer");
    failed |= check(bloc_len(c) == 2u * HEADER_SIZE + sizeof(payload), "length after prepends");
    failed |= check(memcmp(bloc_data(c), bloc_data(b), HEADER_SIZE) == 0, "prefix copied");
    failed |= check(bloc_release(c) == BLOC_OK, "release second buffer");

    /* 8. Final release returns the block; the handle must not be used afterwards. */
    failed |= check(bloc_release(b) == BLOC_OK, "final release");
    b = NULL;
    failed |= check(bloc_pool_free_count(&pool) == PACKET_COUNT, "all blocks free again");
    failed |= check(bloc_pool_deinit(&pool) == BLOC_OK, "pool deinit");

    return failed;
}
