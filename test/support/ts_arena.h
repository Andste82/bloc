/* Guarded storage arena for pool storage used by the tests. */
#ifndef TS_ARENA_H
#define TS_ARENA_H

#include <stddef.h>
#include <stdint.h>

#define TS_ARENA_SIZE ((size_t)4u * 1024u * 1024u)
#define TS_ARENA_GUARD 64u
#define TS_ARENA_MAX_REGIONS 1024u
#define TS_ARENA_GUARD_BYTE 0xA5u
#define TS_ARENA_FILL_BYTE 0x5Au

/*
 * Returns a region of `size` bytes whose address is aligned to BLOC_STORAGE_ALIGNMENT and then
 * shifted by `misalign` bytes. The region is filled with TS_ARENA_FILL_BYTE and surrounded by
 * TS_ARENA_GUARD bytes of TS_ARENA_GUARD_BYTE on both sides. Returns NULL if the arena or the
 * region table is exhausted.
 */
uint8_t *ts_storage(size_t size, size_t misalign);

/* Forgets all regions. Called from setUp(). */
void ts_arena_reset(void);

/* Number of regions handed out since the last reset. */
size_t ts_arena_region_count(void);

/* True if every guard band of every region is intact. */
int ts_arena_guards_ok(void);

/* Fails the running Unity test if a guard band was modified. Called from tearDown(). */
void ts_arena_check(void);

#endif /* TS_ARENA_H */
