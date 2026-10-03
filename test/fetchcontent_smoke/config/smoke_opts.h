/* Consumer configuration header of the FetchContent smoke test (FC-03). */
#ifndef BLOC_SMOKE_OPTS_H
#define BLOC_SMOKE_OPTS_H

#include <stdint.h>

/* Every type is at least as wide as the default, so a deliberate mismatch with the default
 * layout (FC-04) never makes the library write past the consumer's larger structures. */
#define BLOC_BLOCK_ALIGNMENT 32
#define BLOC_PAYLOAD_ALIGNMENT 16
#define BLOC_SIZE_T uint32_t
#define BLOC_COUNT_T uint16_t
#define BLOC_STATS 1

#endif /* BLOC_SMOKE_OPTS_H */
