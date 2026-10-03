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
# Normal variable, read by option(BLOC_WERROR) in src/ (policy CMP0077): warnings are errors in
# BLOC's own build, a consumer gets the option's default OFF.
if(BLOC_IS_TOP_LEVEL AND NOT DEFINED BLOC_WERROR)
  set(BLOC_WERROR ON)
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

# The library itself: the same file that consumers add with FetchContent (SOURCE_SUBDIR src).
add_subdirectory(src)

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
  # The examples are plain programs without the test support library. The test configurations
  # other than "default" put hooks (assertion handler, lock) into the bloc target that only the
  # test support provides, so the examples are built with the default configuration only.
  if(BLOC_IS_TOP_LEVEL AND NOT BLOC_TEST_CONFIG STREQUAL "default")
    message(STATUS "BLOC examples are skipped for BLOC_TEST_CONFIG=${BLOC_TEST_CONFIG}")
  else()
    add_subdirectory(examples)
  endif()
endif()
