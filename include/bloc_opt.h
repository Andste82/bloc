/*
 * BLOC configuration defaults and compile-time validation.
 *
 * Every option can be overridden in a project configuration header named by
 * BLOC_CONFIG_HEADER, which bloc.h includes before this file.
 */
#ifndef BLOC_OPT_H
#define BLOC_OPT_H

#include <stddef.h>
#include <stdint.h>

/* --- Defaults --------------------------------------------------------------------------- */

#ifndef BLOC_BLOCK_ALIGNMENT
#define BLOC_BLOCK_ALIGNMENT 4
#endif

#ifndef BLOC_PAYLOAD_ALIGNMENT
#define BLOC_PAYLOAD_ALIGNMENT 4
#endif

#ifndef BLOC_SIZE_T
#define BLOC_SIZE_T uint16_t
#endif

#ifndef BLOC_COUNT_T
#define BLOC_COUNT_T uint8_t
#endif

#ifndef BLOC_REFCOUNT_T
#define BLOC_REFCOUNT_T uint8_t
#endif

#ifndef BLOC_THREAD_SAFE
#define BLOC_THREAD_SAFE 0
#endif

#ifndef BLOC_CHECKS
#define BLOC_CHECKS 1
#endif

#ifndef BLOC_DEBUG
#define BLOC_DEBUG 0
#endif

#ifndef BLOC_STATS
#define BLOC_STATS 0
#endif

/* Default assertion handler: traps without calling the C library. */
#ifndef BLOC_PLATFORM_ASSERT
#if defined(__GNUC__) || defined(__clang__)
#define BLOC_PLATFORM_ASSERT(msg)                                                                  \
    do {                                                                                           \
        (void)(msg);                                                                               \
        __builtin_trap();                                                                          \
    } while (0)
#else
#define BLOC_PLATFORM_ASSERT(msg)                                                                  \
    do {                                                                                           \
        (void)(msg);                                                                               \
        for (;;) {                                                                                 \
        }                                                                                          \
    } while (0)
#endif
#endif

/* --- Validation ------------------------------------------------------------------------- */

#if BLOC_DEBUG && !BLOC_CHECKS
#error "BLOC_DEBUG requires BLOC_CHECKS"
#endif

#if BLOC_THREAD_SAFE &&                                                                            \
    !(defined(BLOC_DECL_PROTECT) && defined(BLOC_PROTECT) && defined(BLOC_UNPROTECT))
#error "BLOC_THREAD_SAFE requires BLOC_DECL_PROTECT, BLOC_PROTECT and BLOC_UNPROTECT"
#endif

#define BLOC_IS_POW2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))

_Static_assert(BLOC_IS_POW2(BLOC_BLOCK_ALIGNMENT), "BLOC_BLOCK_ALIGNMENT must be a power of two");
_Static_assert(BLOC_IS_POW2(BLOC_PAYLOAD_ALIGNMENT),
               "BLOC_PAYLOAD_ALIGNMENT must be a power of two");
_Static_assert((BLOC_SIZE_T)-1 > 0, "BLOC_SIZE_T must be unsigned");
_Static_assert((BLOC_COUNT_T)-1 > 0, "BLOC_COUNT_T must be unsigned");
_Static_assert((BLOC_REFCOUNT_T)-1 > 0, "BLOC_REFCOUNT_T must be unsigned");
_Static_assert(sizeof(BLOC_SIZE_T) <= sizeof(size_t), "BLOC_SIZE_T must not be wider than size_t");

#endif /* BLOC_OPT_H */
