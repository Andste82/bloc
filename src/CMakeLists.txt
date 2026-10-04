# BLOC library: the target bloc (STATIC) with the alias bloc::bloc, and nothing else.
#
# This file is the unit that other projects consume. It declares no project(), no tests and no
# options beyond the BLOC_* entries below, so adding it has no side effects on the consumer
# (plan section 3.8). It is used in three ways:
#   - FetchContent:     FetchContent_Declare(bloc GIT_REPOSITORY ... SOURCE_SUBDIR src)
#   - add_subdirectory: add_subdirectory(<path to bloc>/src bloc)
#   - BLOC's own top-level CMakeLists.txt, which adds the tests and instrumentation on top.
cmake_minimum_required(VERSION 3.20...4.2)

# Project configuration (spec section 6). Guarded so that a consumer's normal variable wins on the
# first configure too. (Under CMake 3.20, policy CMP0126 does not exist and set(CACHE) would hide
# it.)
if(NOT DEFINED BLOC_CONFIG_HEADER)
  set(BLOC_CONFIG_HEADER "" CACHE STRING "BLOC project configuration header, e.g. bloc_opts.h")
endif()
if(NOT DEFINED BLOC_CONFIG_DIRS)
  set(BLOC_CONFIG_DIRS "" CACHE STRING "Absolute include directories containing BLOC_CONFIG_HEADER")
endif()
option(BLOC_WERROR "Treat warnings in BLOC sources as errors" OFF)

add_library(bloc STATIC bloc.c)
add_library(bloc::bloc ALIAS bloc)
target_include_directories(bloc PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_compile_features(bloc PUBLIC c_std_11)
set_target_properties(bloc PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

# The configuration header is a PUBLIC definition of the library, never of the consumer alone, so
# the library and every consumer translation unit see the same struct layouts (CM-06).
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
  $<$<C_COMPILER_ID:Clang>:-fno-builtin-memcpy>
  # One section per function and object, so that a consumer linking with --gc-sections keeps only
  # the functions it calls (spec section 17, rule 2), whatever flags the consumer uses itself.
  # The optimization level stays the consumer's choice.
  $<$<C_COMPILER_ID:GNU,Clang>:-ffunction-sections>
  $<$<C_COMPILER_ID:GNU,Clang>:-fdata-sections>)
if(BLOC_WERROR)
  target_compile_options(bloc PRIVATE $<$<C_COMPILER_ID:GNU,Clang>:-Werror>)
endif()
