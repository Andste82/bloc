# Examples. They use only the public API and link against the library target, so they are built
# with whatever project configuration the bloc target carries (CM-06).
add_executable(bloc_example_basic basic.c)
target_link_libraries(bloc_example_basic PRIVATE bloc::bloc)
if(COMMAND bloc_instrument)
  # Top-level build: coverage, sanitizer and LTO flags must match the library's.
  bloc_instrument(bloc_example_basic)
endif()

if(BLOC_IS_TOP_LEVEL)
  # The example is also a smoke test, and the sample configuration header must keep compiling.
  # The header is compiled in its own object, without the library's configuration.
  add_test(NAME bloc_example_basic COMMAND bloc_example_basic)
  add_library(bloc_example_opts OBJECT opts_check.c)
  target_include_directories(bloc_example_opts PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/include" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_definitions(bloc_example_opts PRIVATE "BLOC_CONFIG_HEADER=\"bloc_opts_example.h\"")
  set_target_properties(bloc_example_opts PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
                        C_EXTENSIONS OFF)
  target_compile_options(bloc_example_opts PRIVATE
    "$<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Werror>")
endif()
