# Stand-alone consumer of BLOC (FetchContent smoke test, plan section 9.8).
# It is never added by BLOC's own CMakeLists.txt; scripts/fetchcontent_smoke.sh configures it from
# outside, like a real user project.
cmake_minimum_required(VERSION 3.20...4.2)
project(bloc_smoke LANGUAGES C)

# Variant switches, set by scripts/fetchcontent_smoke.sh.
set(SMOKE_CONFIG_MODE "none" CACHE STRING "How the consumer passes its BLOC configuration: none, normal or cache")
set(SMOKE_CONFIG_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/config" CACHE STRING "BLOC_CONFIG_DIRS used by the config variants")
option(SMOKE_MISMATCH "Also build smoke_mismatch: the config header only on the executable (FC-04)" OFF)

if(NOT DEFINED BLOC_GIT_TAG)
  set(BLOC_GIT_TAG main)
endif()
if(NOT DEFINED BLOC_GIT_SHALLOW)
  set(BLOC_GIT_SHALLOW TRUE)
endif()

if(SMOKE_CONFIG_MODE STREQUAL "normal")
  set(BLOC_CONFIG_HEADER "smoke_opts.h")
  set(BLOC_CONFIG_DIRS "${SMOKE_CONFIG_DIRS}")
elseif(SMOKE_CONFIG_MODE STREQUAL "cache")
  set(BLOC_CONFIG_HEADER "smoke_opts.h" CACHE STRING "")
  set(BLOC_CONFIG_DIRS "${SMOKE_CONFIG_DIRS}" CACHE STRING "")
elseif(NOT SMOKE_CONFIG_MODE STREQUAL "none")
  message(FATAL_ERROR "SMOKE_CONFIG_MODE must be none, normal or cache")
endif()

# Snapshot for the hygiene assertions of FC-02.
get_cmake_property(_smoke_cache_before CACHE_VARIABLES)
set(_smoke_flags_before "${CMAKE_C_FLAGS}")
get_property(_smoke_opts_before DIRECTORY PROPERTY COMPILE_OPTIONS)
get_property(_smoke_defs_before DIRECTORY PROPERTY COMPILE_DEFINITIONS)
get_property(_smoke_dirs_before DIRECTORY PROPERTY INCLUDE_DIRECTORIES)

# >>> bloc-fetchcontent  (this block is kept identical in README.md and test/fetchcontent_smoke/CMakeLists.txt, FC-10)
include(FetchContent)
FetchContent_Declare(bloc
  GIT_REPOSITORY https://github.com/Andste82/bloc.git
  GIT_TAG        ${BLOC_GIT_TAG}   # a release tag such as v1.0.0, or a full commit hash
  GIT_SHALLOW    ${BLOC_GIT_SHALLOW})
# optional project configuration (spec section 6):
# set(BLOC_CONFIG_HEADER "bloc_opts.h")
# set(BLOC_CONFIG_DIRS   "${CMAKE_CURRENT_SOURCE_DIR}/config")
FetchContent_MakeAvailable(bloc)
# <<< bloc-fetchcontent

# --- FC-02: consumer-visible result of FetchContent_MakeAvailable(bloc) --------------------------
# smoke_require(<message> <condition>...): FATAL_ERROR unless the condition holds. Conditions use
# variable names and unquoted words only, because they are expanded into if().
function(smoke_require message)
  if(NOT (${ARGN}))
    message(FATAL_ERROR "FC-02: ${message}")
  endif()
endfunction()

get_property(_smoke_targets DIRECTORY "${bloc_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
smoke_require("BLOC defines targets '${_smoke_targets}', expected exactly 'bloc' (CM-01)"
              _smoke_targets STREQUAL bloc)
get_property(_smoke_subdirs DIRECTORY "${bloc_SOURCE_DIR}" PROPERTY SUBDIRECTORIES)
smoke_require("BLOC added subdirectories: ${_smoke_subdirs} (CM-01)" NOT _smoke_subdirs)
smoke_require("the unity target exists (CM-01)" NOT TARGET unity)
FetchContent_GetProperties(unity POPULATED _smoke_unity_populated)
smoke_require("Unity was downloaded (CM-01)" NOT _smoke_unity_populated)

smoke_require("bloc::bloc does not exist (CM-01)" TARGET bloc::bloc)
get_target_property(_smoke_type bloc TYPE)
smoke_require("bloc is of type ${_smoke_type}, expected STATIC_LIBRARY (CM-10)"
              _smoke_type STREQUAL STATIC_LIBRARY)
get_target_property(_smoke_link_libs bloc INTERFACE_LINK_LIBRARIES)
smoke_require("INTERFACE_LINK_LIBRARIES of bloc is '${_smoke_link_libs}' (CM-02)"
              NOT _smoke_link_libs)
get_target_property(_smoke_iface_opts bloc INTERFACE_COMPILE_OPTIONS)
smoke_require("INTERFACE_COMPILE_OPTIONS of bloc is '${_smoke_iface_opts}' (CM-02)"
              NOT _smoke_iface_opts)
get_target_property(_smoke_iface_features bloc INTERFACE_COMPILE_FEATURES)
smoke_require("INTERFACE_COMPILE_FEATURES of bloc is '${_smoke_iface_features}' (CM-02)"
              _smoke_iface_features STREQUAL c_std_11)

get_target_property(_smoke_bloc_opts bloc COMPILE_OPTIONS)
foreach(_smoke_bad IN ITEMS -Werror --coverage -fsanitize -flto)
  string(FIND "${_smoke_bloc_opts}" "${_smoke_bad}" _smoke_pos)
  smoke_require("COMPILE_OPTIONS of bloc contain ${_smoke_bad} (CM-02)" _smoke_pos EQUAL -1)
endforeach()

get_target_property(_smoke_iface_defs bloc INTERFACE_COMPILE_DEFINITIONS)
if(SMOKE_CONFIG_MODE STREQUAL "none")
  smoke_require("INTERFACE_COMPILE_DEFINITIONS of bloc is '${_smoke_iface_defs}' without configuration (CM-02)"
                NOT _smoke_iface_defs)
else()
  set(_smoke_expected_defs "BLOC_CONFIG_HEADER=\"smoke_opts.h\"")
  smoke_require("INTERFACE_COMPILE_DEFINITIONS of bloc is '${_smoke_iface_defs}', expected ${_smoke_expected_defs} (CM-06)"
                _smoke_iface_defs STREQUAL _smoke_expected_defs)
endif()

get_cmake_property(_smoke_cache_after CACHE_VARIABLES)
set(_smoke_new_cache ${_smoke_cache_after})
list(REMOVE_ITEM _smoke_new_cache ${_smoke_cache_before})
# CMake itself records the version of a sub-project that declares one in the STATIC cache entries
# CMAKE_PROJECT_VERSION*, so project(bloc VERSION ...) (CM-09) makes them appear; they are not
# BLOC cache entries (docs/OPEN_QUESTIONS.md, entry OQ-001). GIT_EXECUTABLE is created by
# FetchContent itself when it really downloads with git (FC-09); the offline variants never see it.
foreach(_smoke_entry IN LISTS _smoke_new_cache)
  smoke_require("BLOC added the cache entry ${_smoke_entry} (CM-04)"
                _smoke_entry MATCHES "^(BLOC_|bloc_|FETCHCONTENT_|CMAKE_PROJECT_VERSION|GIT_EXECUTABLE$)")
endforeach()
foreach(_smoke_test_option IN ITEMS BLOC_TEST_CONFIG BLOC_COVERAGE BLOC_SANITIZE BLOC_LTO)
  smoke_require("${_smoke_test_option} is defined in a consumer build (CM-04)"
                NOT DEFINED CACHE{${_smoke_test_option}} AND NOT DEFINED ${_smoke_test_option})
endforeach()

# Properties that are not set give an unset variable; normalize before comparing.
get_property(_smoke_opts_after DIRECTORY PROPERTY COMPILE_OPTIONS)
get_property(_smoke_defs_after DIRECTORY PROPERTY COMPILE_DEFINITIONS)
get_property(_smoke_dirs_after DIRECTORY PROPERTY INCLUDE_DIRECTORIES)
foreach(_smoke_var IN ITEMS _smoke_opts_before _smoke_defs_before _smoke_dirs_before
                            _smoke_opts_after _smoke_defs_after _smoke_dirs_after)
  set(${_smoke_var} "${${_smoke_var}}")
endforeach()
set(_smoke_flags_after "${CMAKE_C_FLAGS}")
smoke_require("CMAKE_C_FLAGS changed (CM-03)" _smoke_flags_after STREQUAL _smoke_flags_before)
smoke_require("directory COMPILE_OPTIONS changed (CM-03)" _smoke_opts_after STREQUAL _smoke_opts_before)
smoke_require("directory COMPILE_DEFINITIONS changed (CM-03)" _smoke_defs_after STREQUAL _smoke_defs_before)
smoke_require("directory INCLUDE_DIRECTORIES changed (CM-03)" _smoke_dirs_after STREQUAL _smoke_dirs_before)

# BLOC's own directory must not have used the directory-wide commands either (CM-03); all flags
# are per target.
foreach(_smoke_prop IN ITEMS COMPILE_OPTIONS COMPILE_DEFINITIONS INCLUDE_DIRECTORIES LINK_OPTIONS
                             LINK_DIRECTORIES)
  get_property(_smoke_value DIRECTORY "${bloc_SOURCE_DIR}" PROPERTY ${_smoke_prop})
  smoke_require("BLOC set the directory property ${_smoke_prop}: '${_smoke_value}' (CM-03)"
                NOT _smoke_value)
endforeach()

# --- The consumer ------------------------------------------------------------------------------
set(_smoke_strict_flags -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)

add_executable(smoke main.c)
target_link_libraries(smoke PRIVATE bloc::bloc)
target_compile_options(smoke PRIVATE ${_smoke_strict_flags})

enable_testing()
add_test(NAME smoke COMMAND smoke)

if(SMOKE_MISMATCH)
  # FC-04: the same main.c against the default-configured library, but with the configuration
  # header on the executable only. The layout cross-check in main.c must notice the mismatch.
  add_executable(smoke_mismatch main.c)
  target_link_libraries(smoke_mismatch PRIVATE bloc::bloc)
  target_compile_options(smoke_mismatch PRIVATE ${_smoke_strict_flags})
  # SMOKE_MISMATCH keeps main.c from calling bloc_pool_get_stats, which the default-configured
  # library does not provide, so that the executable still links and fails at check 2.
  target_compile_definitions(smoke_mismatch PRIVATE "BLOC_CONFIG_HEADER=\"smoke_opts.h\"" SMOKE_MISMATCH=1)
  target_include_directories(smoke_mismatch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/config")
  add_test(NAME smoke_mismatch COMMAND smoke_mismatch)
  set_tests_properties(smoke_mismatch PROPERTIES WILL_FAIL TRUE)
endif()
