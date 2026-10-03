# Examples. They use only the public API and link against the library target, so they are built
# with whatever project configuration the bloc target carries (CM-06).
add_executable(bloc_example_basic basic.c)
target_link_libraries(bloc_example_basic PRIVATE bloc::bloc)
if(COMMAND bloc_instrument)
  # Top-level build: coverage, sanitizer and LTO flags must match the library's.
  bloc_instrument(bloc_example_basic)
endif()
