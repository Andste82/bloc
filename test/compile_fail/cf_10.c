/* Compile-fail test CF-10: the statistics API does not exist with BLOC_STATS == 0. */
#include "bloc/bloc.h"

bloc_status_t cf_10_read(const bloc_pool_t *pool)
{
    bloc_pool_stats_t stats;
    return bloc_pool_get_stats(pool, &stats);
}
