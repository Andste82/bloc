/*
 * BLOC FetchContent smoke test. Scaffolding stage: only compile-time macros are used, so the
 * program checks that the public headers compile under the consumer's strict flags. The run-time
 * checks of plan section 9.8 (layout cross-check, pool lifecycle) are added once the library
 * has functions.
 */
#include "bloc.h"

_Static_assert(BLOC_POOL_SIZE(4, 64) > 0, "the pool size must be positive");

int main(void) { return 0; }
