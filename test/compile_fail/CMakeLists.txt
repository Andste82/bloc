# Compile-fail tests (implementation plan, section 8.1, CF-01..CF-11).
#
# Every case is an OBJECT library that is excluded from the default build. A CTest test builds
# it and passes only if the build output matches the expected diagnostic, so a failure for an
# unrelated reason is caught. The targets are deliberately not linked to bloc::bloc: they must not
# inherit the configuration of the current build directory, each one brings its own header.
#
# Flags are -std=c11 -Werror without -Wpedantic. The regular expressions match the diagnostics
# of both GCC and Clang.
include(CheckCSourceCompiles)

# bloc_add_compile_fail(<id> <regex>): test/compile_fail/cf_<id>.c with cf_<id>_cfg.h.
function(bloc_add_compile_fail id regex)
  set(target compile_fail_cf${id})
  add_library(${target} OBJECT EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/cf_${id}.c")
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_definitions(${target} PRIVATE "BLOC_CONFIG_HEADER=\"cf_${id}_cfg.h\"")
  set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_compile_options(${target} PRIVATE "$<$<C_COMPILER_ID:GNU,Clang>:-std=c11;-Werror>")
  add_test(NAME ${target}
    COMMAND "${CMAKE_COMMAND}" --build "${bloc_BINARY_DIR}" --target ${target})
  set_tests_properties(${target} PROPERTIES
    PASS_REGULAR_EXPRESSION "${regex}"
    # One build tree: concurrent `cmake --build` runs would race on the Ninja state.
    RESOURCE_LOCK bloc_compile_fail)
endfunction()

bloc_add_compile_fail(01 "BLOC_BLOCK_ALIGNMENT must be a power of two")
bloc_add_compile_fail(02 "BLOC_BLOCK_ALIGNMENT must be a power of two")
bloc_add_compile_fail(03 "BLOC_PAYLOAD_ALIGNMENT must be a power of two")
bloc_add_compile_fail(04 "BLOC_SIZE_T must be unsigned")
bloc_add_compile_fail(05 "BLOC_COUNT_T must be unsigned")
bloc_add_compile_fail(06 "BLOC_REFCOUNT_T must be unsigned")
bloc_add_compile_fail(07 "BLOC_THREAD_SAFE requires BLOC_DECL_PROTECT")
bloc_add_compile_fail(08 "BLOC_DEBUG requires BLOC_CHECKS")

# CF-09 needs a type wider than size_t, so it is registered only where the compiler has one.
set(_bloc_saved_target_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
check_c_source_compiles("__extension__ typedef unsigned __int128 wide_t; wide_t w;"
                        BLOC_HAVE_INT128)
set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_bloc_saved_target_type}")
if(BLOC_HAVE_INT128)
  bloc_add_compile_fail(09 "BLOC_SIZE_T must not be wider than size_t")
else()
  message(STATUS "CF-09 not registered: the compiler has no __int128")
endif()

bloc_add_compile_fail(10 "bloc_pool_stats_t|bloc_pool_get_stats")
bloc_add_compile_fail(11 "const|qualifier")
