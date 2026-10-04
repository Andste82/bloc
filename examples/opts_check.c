/*
 * Compiles the sample configuration header together with the public header, so that the sample
 * stays valid. Built only in a top-level build of the project (never linked).
 */
#include "bloc/bloc.h"

/* A declaration is enough: the unit is only compiled, and it keeps the translation unit non-empty.
 */
extern const bloc_size_t bloc_example_opts_probe;
