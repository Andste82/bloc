/* Compile-fail test CF-11: a read-only handle cannot be passed to a mutating function. */
#include "bloc.h"

bloc_status_t cf_11_mutate(bloc_const_handle_t b) { return bloc_set_len(b, 0); }
