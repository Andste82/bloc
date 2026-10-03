#include "ts_pthread.h"

#include <pthread.h>

unsigned ts_pthread_locks;
unsigned ts_pthread_unlocks;

static pthread_mutex_t ts_pthread_mutex = PTHREAD_MUTEX_INITIALIZER;

void ts_pthread_lock(void)
{
    (void)pthread_mutex_lock(&ts_pthread_mutex);
    ts_pthread_locks++;
}

void ts_pthread_unlock(void)
{
    ts_pthread_unlocks++;
    (void)pthread_mutex_unlock(&ts_pthread_mutex);
}
