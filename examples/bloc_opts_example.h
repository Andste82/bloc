/*
 * Example BLOC project configuration header.
 *
 * Copy this file into your project, rename it (for example bloc_opts.h) and select it with the
 * CMake options BLOC_CONFIG_HEADER and BLOC_CONFIG_DIRS (see README.md). The header must be seen
 * by the bloc target and by every consumer: never define BLOC_CONFIG_HEADER on your own target
 * only. Every option is optional; the defaults are in include/bloc_opt.h and are documented in
 * section 6 of docs/BLOC_SPEC.md.
 *
 * The values below describe a small network stack on a microcontroller: statistics for pool
 * sizing, assertions for programming errors and 8-byte aligned blocks.
 */
#ifndef BLOC_OPTS_EXAMPLE_H
#define BLOC_OPTS_EXAMPLE_H

#include <stdint.h>

/* Blocks and payloads are aligned to 8 bytes (for example for 64-bit DMA descriptors). */
#define BLOC_BLOCK_ALIGNMENT 8
#define BLOC_PAYLOAD_ALIGNMENT 8

/* Element sizes and lengths up to 65535 bytes, at most 255 buffers per pool and 255 references. */
#define BLOC_SIZE_T uint16_t
#define BLOC_COUNT_T uint8_t
#define BLOC_REFCOUNT_T uint8_t

/* Parameter and bounds checks return status codes; assertions catch programming errors. */
#define BLOC_CHECKS 1
#define BLOC_DEBUG 1

/* Keep the high-water mark and the allocation-failure counter of every pool. */
#define BLOC_STATS 1

/* Route failed assertions to the application's fault handler (declared by the application). */
void app_fault(const char *msg);
#define BLOC_PLATFORM_ASSERT(msg) app_fault(msg)

/*
 * Thread safety: define all three macros and set BLOC_THREAD_SAFE to 1. Each macro receives the
 * name of a local variable. The macros wrap the short critical sections of the pool and the
 * reference count, for example in interrupt masking or, outside ISRs, in a mutex (spec section
 * 12). They are left disabled here.
 *
 * #define BLOC_THREAD_SAFE 1
 * #define BLOC_DECL_PROTECT(lev) uint32_t lev
 * #define BLOC_PROTECT(lev) do { (lev) = save_and_disable_irq(); } while (0)
 * #define BLOC_UNPROTECT(lev) restore_irq(lev)
 */

#endif /* BLOC_OPTS_EXAMPLE_H */
