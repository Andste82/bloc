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

/** Minimum alignment of every block start; a power of two (default 4). */
#ifndef BLOC_BLOCK_ALIGNMENT
#define BLOC_BLOCK_ALIGNMENT 4
#endif

/** Alignment of the data area start; a power of two, 1 disables it (default 4). */
#ifndef BLOC_PAYLOAD_ALIGNMENT
#define BLOC_PAYLOAD_ALIGNMENT 4
#endif

/** Unsigned type of element sizes, offsets and lengths (default uint16_t). */
#ifndef BLOC_SIZE_T
#define BLOC_SIZE_T uint16_t
#endif

/** Unsigned type of element counts, active count and statistics (default uint8_t). */
#ifndef BLOC_COUNT_T
#define BLOC_COUNT_T uint8_t
#endif

/** Unsigned type of the reference count (default uint8_t). */
#ifndef BLOC_REFCOUNT_T
#define BLOC_REFCOUNT_T uint8_t
#endif

/**
 * 1 = protect pool and refcount updates with BLOC_PROTECT (needs BLOC_DECL_PROTECT, BLOC_PROTECT
 * and BLOC_UNPROTECT; default 0).
 */
#ifndef BLOC_THREAD_SAFE
#define BLOC_THREAD_SAFE 0
#endif

/** 1 = parameter and bounds checks that return status codes (default 1). */
#ifndef BLOC_CHECKS
#define BLOC_CHECKS 1
#endif

/** 1 = assertions for programming errors; requires BLOC_CHECKS (default 0). */
#ifndef BLOC_DEBUG
#define BLOC_DEBUG 0
#endif

/**
 * 1 = BLOC_PLATFORM_ASSERT receives a message string; 0 = it receives a null pointer and no message
 * text is compiled (default 1, BLOC_DEBUG only). Classic AVRs keep constants in RAM, where the full
 * set of messages costs about 2.6 KiB.
 */
#ifndef BLOC_ASSERT_MESSAGES
#define BLOC_ASSERT_MESSAGES 1
#endif

/** 1 = high-water mark and allocation-failure counter (default 0). */
#ifndef BLOC_STATS
#define BLOC_STATS 0
#endif

/**
 * Called when a debug assertion has failed (BLOC_DEBUG only). The default traps without calling
 * the C library; an application may route it to a fault handler or logger. If it returns, BLOC
 * continues as described in the specification, section 13.
 */
#ifndef BLOC_PLATFORM_ASSERT
/* avr-gcc has no trap instruction: __builtin_trap() becomes a call to abort() there. */
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__AVR__)
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

/* C11 keywords with their C++11 spellings, so that the header also compiles as C++. */
#ifdef __cplusplus
#define BLOC_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#define BLOC_ALIGNAS(a) alignas(a)
#define BLOC_ALIGNOF(t) alignof(t)
#else
#define BLOC_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#define BLOC_ALIGNAS(a) _Alignas(a)
#define BLOC_ALIGNOF(t) _Alignof(t)
#endif

/** True if x is a power of two (x != 0), usable in constant expressions. */
#define BLOC_IS_POW2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))

BLOC_STATIC_ASSERT(BLOC_IS_POW2(BLOC_BLOCK_ALIGNMENT),
                   "BLOC_BLOCK_ALIGNMENT must be a power of two");
BLOC_STATIC_ASSERT(BLOC_IS_POW2(BLOC_PAYLOAD_ALIGNMENT),
                   "BLOC_PAYLOAD_ALIGNMENT must be a power of two");
BLOC_STATIC_ASSERT((BLOC_SIZE_T)-1 > 0, "BLOC_SIZE_T must be unsigned");
BLOC_STATIC_ASSERT((BLOC_COUNT_T)-1 > 0, "BLOC_COUNT_T must be unsigned");
BLOC_STATIC_ASSERT((BLOC_REFCOUNT_T)-1 > 0, "BLOC_REFCOUNT_T must be unsigned");
BLOC_STATIC_ASSERT(sizeof(BLOC_SIZE_T) <= sizeof(size_t),
                   "BLOC_SIZE_T must not be wider than size_t");

#endif /* BLOC_OPT_H */
