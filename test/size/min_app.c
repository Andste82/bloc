/*
 * Minimal link program for the dead-stripping check (FP-05, implementation plan section 9.5). It
 * calls only bloc_pool_init, bloc_alloc and bloc_release. Linked with -ffunction-sections and
 * --gc-sections, the final image must contain no other BLOC function.
 */
#include <stddef.h>
#include <stdint.h>

#include "bloc/bloc.h"

static BLOC_POOL_STORAGE(g_storage, 2, 16);
static bloc_pool_t g_pool;

int main(void)
{
    bloc_handle_t b;

    if (bloc_pool_init(&g_pool, g_storage, sizeof(g_storage), 2, 16) != BLOC_OK) {
        return 1;
    }
    b = bloc_alloc(&g_pool, 0);
    return bloc_release(b) == BLOC_OK ? 0 : 2;
}
