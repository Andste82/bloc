/*
 * Footprint configuration "thread safe" for ARM M-profile (spec section 12): PRIMASK-based
 * protect macros with inline assembly. Used only by scripts/check_size.sh; inline assembly is
 * allowed in configuration headers (R-04).
 */
#ifndef BLOC_SIZE_CFG_TS_H
#define BLOC_SIZE_CFG_TS_H

#include <stdint.h>

#define BLOC_THREAD_SAFE 1
#define BLOC_DECL_PROTECT(lev) uint32_t lev
#define BLOC_PROTECT(lev) __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(lev)::"memory")
#define BLOC_UNPROTECT(lev) __asm__ volatile("msr primask, %0" ::"r"(lev) : "memory")

#endif /* BLOC_SIZE_CFG_TS_H */
