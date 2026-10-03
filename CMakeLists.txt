cmake_minimum_required(VERSION 3.20...4.2)
project(bloc VERSION 1.0.0 LANGUAGES C) # C only: consumers need no C++ compiler

# PROJECT_IS_TOP_LEVEL needs CMake 3.21; the floor is 3.20, so detect it directly.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(BLOC_IS_TOP_LEVEL ON)
else()
  set(BLOC_IS_TOP_LEVEL OFF)
endif()

option(BLOC_BUILD_TESTS    "Build the BLOC unit tests"                ${BLOC_IS_TOP_LEVEL})
option(BLOC_BUILD_EXAMPLES "Build the BLOC examples"                  ${BLOC_IS_TOP_LEVEL})
option(BLOC_WERROR         "Treat warnings in BLOC sources as errors" ${BLOC_IS_TOP_LEVEL})

# Guarded so that a consumer's normal variable wins on the first configure too.
# (Under CMake 3.20, policy CMP0126 does not exist and set(CACHE) would hide it.)
if(NOT DEFINED BLOC_CONFIG_HEADER)
  set(BLOC_CONFIG_HEADER "" CACHE STRING "BLOC project configuration header, e.g. bloc_opts.h")
endif()
if(NOT DEFINED BLOC_CONFIG_DIRS)
  set(BLOC_CONFIG_DIRS "" CACHE STRING "Absolute include directories containing BLOC_CONFIG_HEADER")
endif()

if(BLOC_IS_TOP_LEVEL)
  # These options exist only in a top-level build (CM-04). BLOC_TEST_CONFIG maps onto
  # BLOC_CONFIG_HEADER / BLOC_CONFIG_DIRS, the same mechanism a consumer uses (CM-06).
  set(BLOC_TEST_CONFIG "default" CACHE STRING "Test configuration (test/configs/cfg_<name>.h)")
  set_property(CACHE BLOC_TEST_CONFIG PROPERTY STRINGS
               default debug nochecks wide noalign bigalign pthread)
  option(BLOC_COVERAGE "Build library and tests with GCC coverage instrumentation" OFF)
  set(BLOC_SANITIZE "" CACHE STRING "Comma list for -fsanitize=, e.g. address,undefined or thread")
  option(BLOC_LTO "Build library, test support and tests with link-time optimization" OFF)

  include("${CMAKE_CURRENT_LIST_DIR}/cmake/BlocInstrumentation.cmake")

  get_property(_bloc_test_configs CACHE BLOC_TEST_CONFIG PROPERTY STRINGS)
  if(NOT BLOC_TEST_CONFIG IN_LIST _bloc_test_configs)
    list(JOIN _bloc_test_configs ", " _bloc_test_configs_text)
    message(FATAL_ERROR "BLOC_TEST_CONFIG must be one of: ${_bloc_test_configs_text}")
  endif()
  if(NOT BLOC_TEST_CONFIG STREQUAL "default")
    if(BLOC_CONFIG_HEADER)
      message(FATAL_ERROR "BLOC_TEST_CONFIG and BLOC_CONFIG_HEADER are mutually exclusive")
    endif()
    # Normal variables on purpose: they must not stick in the cache between configures.
    set(BLOC_CONFIG_HEADER "cfg_${BLOC_TEST_CONFIG}.h")
    set(BLOC_CONFIG_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/test/configs;${CMAKE_CURRENT_SOURCE_DIR}/test/support")
  endif()
endif()

add_library(bloc STATIC src/bloc.c)
add_library(bloc::bloc ALIAS bloc)
target_include_directories(bloc PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_compile_features(bloc PUBLIC c_std_11)
set_target_properties(bloc PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(BLOC_CONFIG_HEADER)
  foreach(_bloc_dir IN LISTS BLOC_CONFIG_DIRS)
    if(NOT IS_ABSOLUTE "${_bloc_dir}")
      message(FATAL_ERROR "BLOC_CONFIG_DIRS entries must be absolute paths, got \"${_bloc_dir}\"")
    endif()
  endforeach()
  target_compile_definitions(bloc PUBLIC "BLOC_CONFIG_HEADER=\"${BLOC_CONFIG_HEADER}\"")
  target_include_directories(bloc PUBLIC ${BLOC_CONFIG_DIRS})
endif()

set(_bloc_warning_flags
    -std=c11 -Wall -Wextra -Wpedantic
    -Wconversion -Wsign-conversion -Wshadow -Wundef -Wvla -Wpointer-arith
    -Wstrict-prototypes -Wmissing-prototypes -Wcast-align -fno-common)
target_compile_options(bloc PRIVATE
  "$<$<C_COMPILER_ID:GNU,Clang>:${_bloc_warning_flags}>"
  $<$<C_COMPILER_ID:GNU>:-Wcast-align=strict>
  # Clang lowers a memset call to __aeabi_memclr and a memcpy call to __aeabi_memcpy on ARM EABI
  # targets, but the library may reference only memcpy and memset (R-03). PRIVATE, so it never
  # reaches a consumer (CM-02).
  $<$<C_COMPILER_ID:Clang>:-fno-builtin-memset>
  $<$<C_COMPILER_ID:Clang>:-fno-builtin-memcpy>)
if(BLOC_WERROR)
  target_compile_options(bloc PRIVATE $<$<C_COMPILER_ID:GNU,Clang>:-Werror>)
endif()
if(BLOC_IS_TOP_LEVEL)
  # Release is -O2 for BLOC's own build (CMake's default would be -O3).
  target_compile_options(bloc PRIVATE
    $<$<AND:$<CONFIG:Release>,$<C_COMPILER_ID:GNU,Clang>>:-O2>)
  bloc_instrument(bloc)
endif()

if(BLOC_BUILD_TESTS)
  if(NOT BLOC_IS_TOP_LEVEL)
    message(FATAL_ERROR "BLOC tests can only be built when BLOC is the top-level project")
  endif()
  enable_testing()
  add_subdirectory(test) # Unity is fetched in here, never above
endif()
if(BLOC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
