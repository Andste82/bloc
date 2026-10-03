/* Global mutex behind BLOC_PROTECT in the pthread test configuration. */
#ifndef TS_PTHREAD_H
#define TS_PTHREAD_H

void ts_pthread_lock(void);
void ts_pthread_unlock(void);

/* Number of completed lock and unlock calls (read while no other thread is running). */
extern unsigned ts_pthread_locks;
extern unsigned ts_pthread_unlocks;

#endif /* TS_PTHREAD_H */
