/*
 * Compile-time layout checks (XC-05). This file contains only _Static_asserts and one array
 * definition, so it can be compiled for every target without executing anything. It covers the
 * compile-time parts of CFG-01..07, CFG-09 and CFG-11.
 *
 * Expected values are computed from sizeof, _Alignof and the formulas of the specification,
 * section 4, in uintmax_t arithmetic with a division instead of the bit masks of the library
 * macros, so a mistake in the macros cannot cancel out in the check.
 */
#include <stddef.h>
#include <stdint.h>

#include "bloc.h"

#define LS_UP(x, a) ((((uintmax_t)(x) + (uintmax_t)(a) - 1u) / (uintmax_t)(a)) * (uintmax_t)(a))
#define LS_MAXU(a, b) ((uintmax_t)(a) > (uintmax_t)(b) ? (uintmax_t)(a) : (uintmax_t)(b))

#define LS_PA ((uintmax_t)BLOC_PAYLOAD_ALIGNMENT)
#define LS_BA ((uintmax_t)BLOC_BLOCK_ALIGNMENT)
#define LS_SA LS_MAXU(LS_MAXU(LS_BA, LS_PA), _Alignof(struct bloc_handle))
#define LS_HEADER LS_UP(sizeof(struct bloc_handle), LS_PA)
#define LS_STRIDE(e) LS_UP(LS_HEADER + (uintmax_t)(e), LS_SA)

/* CFG-01: BLOC_POOL_SIZE is an integer constant expression (array size and static assertion). */
BLOC_POOL_STORAGE(bloc_layout_static_storage, 3, 10);
_Static_assert(BLOC_POOL_SIZE(3, 10) > 0u, "BLOC_POOL_SIZE must be a constant expression");

/* CFG-09: BLOC_POOL_STORAGE yields an array of the right size. */
_Static_assert(sizeof(bloc_layout_static_storage) == LS_STRIDE(10) * 3u,
               "BLOC_POOL_STORAGE has the wrong size");

/* CFG-03: storage alignment is the maximum of the three alignments and a power of two. */
_Static_assert(BLOC_STORAGE_ALIGNMENT == LS_SA, "BLOC_STORAGE_ALIGNMENT is not the maximum");
_Static_assert(BLOC_STORAGE_ALIGNMENT >= BLOC_BLOCK_ALIGNMENT, "storage alignment below BA");
_Static_assert(BLOC_STORAGE_ALIGNMENT >= BLOC_PAYLOAD_ALIGNMENT, "storage alignment below PA");
_Static_assert(BLOC_STORAGE_ALIGNMENT >= _Alignof(struct bloc_handle),
               "storage alignment below the handle alignment");
_Static_assert((BLOC_STORAGE_ALIGNMENT & (BLOC_STORAGE_ALIGNMENT - 1u)) == 0u,
               "storage alignment is not a power of two");

/* CFG-04: the header holds the handle and is a multiple of the payload alignment. */
_Static_assert(BLOC_HEADER_SIZE == LS_HEADER, "BLOC_HEADER_SIZE does not match the formula");
_Static_assert(BLOC_HEADER_SIZE >= sizeof(struct bloc_handle), "header smaller than the handle");
_Static_assert(BLOC_HEADER_SIZE % BLOC_PAYLOAD_ALIGNMENT == 0u, "header not payload-aligned");

/* CFG-02: BLOC_POOL_SIZE(n, e) == n * stride(e) for the element sizes of the catalogue. */
#define LS_CHECK_POOL_SIZE(n, e)                                                                   \
    _Static_assert(BLOC_BLOCK_STRIDE(e) == LS_STRIDE(e), "BLOC_BLOCK_STRIDE mismatch");            \
    _Static_assert(BLOC_POOL_SIZE(n, e) == (uintmax_t)(n) * LS_STRIDE(e), "BLOC_POOL_SIZE "        \
                                                                          "mismatch")

#define LS_CHECK_ALL_COUNTS(e)                                                                     \
    LS_CHECK_POOL_SIZE(1, e);                                                                      \
    LS_CHECK_POOL_SIZE(2, e);                                                                      \
    LS_CHECK_POOL_SIZE(7, e)

LS_CHECK_ALL_COUNTS(1);
LS_CHECK_ALL_COUNTS(BLOC_PAYLOAD_ALIGNMENT - 1);
LS_CHECK_ALL_COUNTS(BLOC_PAYLOAD_ALIGNMENT);
LS_CHECK_ALL_COUNTS(BLOC_PAYLOAD_ALIGNMENT + 1);
LS_CHECK_ALL_COUNTS(BLOC_STORAGE_ALIGNMENT - 1);
LS_CHECK_ALL_COUNTS(BLOC_STORAGE_ALIGNMENT);
LS_CHECK_ALL_COUNTS(BLOC_STORAGE_ALIGNMENT + 1);
LS_CHECK_ALL_COUNTS(255);

/* CFG-05: field order link, offset, len, refcount; the free-list pointer never overlays refcount.
 */
_Static_assert(offsetof(struct bloc_handle, link) == 0u, "link must be the first field");
_Static_assert(offsetof(struct bloc_handle, offset) > offsetof(struct bloc_handle, link),
               "offset must follow link");
_Static_assert(offsetof(struct bloc_handle, len) > offsetof(struct bloc_handle, offset),
               "len must follow offset");
_Static_assert(offsetof(struct bloc_handle, refcount) > offsetof(struct bloc_handle, len),
               "refcount must follow len");
_Static_assert(offsetof(struct bloc_handle, refcount) >= sizeof(void *),
               "the free-list pointer must not overlay refcount");

/* CFG-06: the maxima are (T)-1 of the configured types. */
_Static_assert(BLOC_SIZE_MAX == (bloc_size_t)-1, "BLOC_SIZE_MAX mismatch");
_Static_assert(BLOC_COUNT_MAX == (bloc_count_t)-1, "BLOC_COUNT_MAX mismatch");
_Static_assert(BLOC_REFCOUNT_MAX == (bloc_refcount_t)-1, "BLOC_REFCOUNT_MAX mismatch");
_Static_assert(BLOC_SIZE_MAX > 0, "bloc_size_t must be unsigned");
_Static_assert(BLOC_COUNT_MAX > 0, "bloc_count_t must be unsigned");
_Static_assert(BLOC_REFCOUNT_MAX > 0, "bloc_refcount_t must be unsigned");

/* CFG-07: BLOC_ELEMENT_SIZE_MAX is the largest e whose stride fits bloc_size_t. */
_Static_assert(LS_STRIDE(BLOC_ELEMENT_SIZE_MAX) <= (uintmax_t)BLOC_SIZE_MAX,
               "the stride of BLOC_ELEMENT_SIZE_MAX does not fit bloc_size_t");
_Static_assert(LS_STRIDE((uintmax_t)BLOC_ELEMENT_SIZE_MAX + 1u) > (uintmax_t)BLOC_SIZE_MAX,
               "the stride of BLOC_ELEMENT_SIZE_MAX + 1 still fits bloc_size_t");

/* CFG-11: status codes in the specified order; BLOC_EMPTY does not exist (see test_layout.c). */
_Static_assert(BLOC_OK == 0, "BLOC_OK must be 0");
_Static_assert(BLOC_INVALID == 1, "BLOC_INVALID must be 1");
_Static_assert(BLOC_BOUNDS == 2, "BLOC_BOUNDS must be 2");
_Static_assert(BLOC_BUSY == 3, "BLOC_BUSY must be 3");
_Static_assert(BLOC_OVERFLOW == 4, "BLOC_OVERFLOW must be 4");
_Static_assert(BLOC_ALIGNMENT == 5, "BLOC_ALIGNMENT must be 5");
