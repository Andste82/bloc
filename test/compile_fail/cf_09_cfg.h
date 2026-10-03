/* Compile-fail configuration 09 */
#include <stdint.h>
/* A type wider than size_t on every target that has it; __extension__ keeps -Wpedantic quiet. */
__extension__ typedef unsigned __int128 cf09_wide_t;
#define BLOC_SIZE_T cf09_wide_t
