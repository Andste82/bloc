include(FetchContent)
FetchContent_Declare(unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG        v2.6.1
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(unity)

# Unity ships with -Werror; it is third-party code and must not fail our build.
target_compile_options(unity PRIVATE $<$<C_COMPILER_ID:GNU,Clang>:-Wno-error>)
# Route Unity's output through ts_unity_putchar() (support/ts_output.c), so that the
# self-tests of the support library can trap expected failures silently.
target_compile_definitions(unity PUBLIC
  UNITY_OUTPUT_CHAR=ts_unity_putchar
  "UNITY_OUTPUT_CHAR_HEADER_DECLARATION=ts_unity_putchar(int)")
bloc_instrument(unity NO_COVERAGE NO_LTO)

# Set by scripts/ci/build_one.sh for the emulated targets (EM-04).
set(BLOC_EXPECT_BIG_ENDIAN "" CACHE STRING "Expected byte order for test_platform (1 = big endian)")
set(BLOC_EXPECT_PTR_BITS   "" CACHE STRING "Expected pointer width for test_platform")

set(_bloc_test_warning_flags -std=c11 -Wall -Wextra -Wpedantic)

# bloc_test_flags(<target>): warning, standard and instrumentation flags of a test target.
function(bloc_test_flags target)
  set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_compile_options(${target} PRIVATE
    "$<$<C_COMPILER_ID:GNU,Clang>:${_bloc_test_warning_flags}>"
    $<$<AND:$<CONFIG:Release>,$<C_COMPILER_ID:GNU,Clang>>:-O2>)
  if(BLOC_WERROR)
    target_compile_options(${target} PRIVATE $<$<C_COMPILER_ID:GNU,Clang>:-Werror>)
  endif()
  bloc_instrument(${target})
endfunction()

# Test support library (section 5 of the implementation plan).
set(_bloc_support_sources
  support/ts_arena.c support/ts_assert.c support/ts_lock.c support/ts_helpers.c support/ts_pool.c
  support/ts_snap.c)
if(BLOC_TEST_CONFIG STREQUAL "pthread")
  list(APPEND _bloc_support_sources support/ts_pthread.c)
endif()
add_library(bloc_test_support STATIC ${_bloc_support_sources})
target_include_directories(bloc_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(bloc_test_support PUBLIC bloc::bloc unity)
if(BLOC_TEST_CONFIG STREQUAL "pthread")
  find_package(Threads REQUIRED)
  target_link_libraries(bloc_test_support PUBLIC Threads::Threads)
endif()
bloc_test_flags(bloc_test_support)

# bloc_add_test(<name> <sources>...): one Unity executable, registered with CTest.
function(bloc_add_test name)
  add_executable(${name} ${ARGN} "${CMAKE_CURRENT_SOURCE_DIR}/support/ts_output.c")
  target_link_libraries(${name} PRIVATE bloc_test_support bloc::bloc unity)
  bloc_test_flags(${name})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

file(GLOB _bloc_test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.c")
foreach(_src IN LISTS _bloc_test_sources)
  get_filename_component(_name "${_src}" NAME_WE)
  if(_name STREQUAL "test_platform")
    continue()
  endif()
  bloc_add_test(${_name} "${_src}")
  if(_name STREQUAL "test_layout")
    # The compile-time layout checks of the cross targets (XC-05) also run on every host and
    # emulated build, in every configuration, including the ones tier 2 does not build.
    target_sources(${_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/target/layout_static.c")
  endif()
endforeach()

add_subdirectory(compile_fail)

# EM-04: only built when the target table provides the expectations.
if(NOT BLOC_EXPECT_BIG_ENDIAN STREQUAL "" OR NOT BLOC_EXPECT_PTR_BITS STREQUAL "")
  if(BLOC_EXPECT_BIG_ENDIAN STREQUAL "" OR BLOC_EXPECT_PTR_BITS STREQUAL "")
    message(FATAL_ERROR "BLOC_EXPECT_BIG_ENDIAN and BLOC_EXPECT_PTR_BITS must be set together")
  endif()
  bloc_add_test(test_platform "${CMAKE_CURRENT_SOURCE_DIR}/test_platform.c")
  target_compile_definitions(test_platform PRIVATE
    BLOC_EXPECT_BIG_ENDIAN=${BLOC_EXPECT_BIG_ENDIAN}
    BLOC_EXPECT_PTR_BITS=${BLOC_EXPECT_PTR_BITS})
endif()

# CFG-12: C++ consumer of the public headers (spec section 15). Built only for host targets: the
# cross toolchains of the CI images have no C++ compiler. The C++ compiler must match the C compiler
# (g++ for gcc, clang++ for clang), so that coverage, sanitizer and LTO runtimes agree.
if(NOT CMAKE_CROSSCOMPILING)
  if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
    get_filename_component(_bloc_cc_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(_bloc_cc_name "${CMAKE_C_COMPILER}" NAME)
    string(REGEX REPLACE "^gcc" "g++" _bloc_cxx_name "${_bloc_cc_name}")
    string(REGEX REPLACE "^clang" "clang++" _bloc_cxx_name "${_bloc_cxx_name}")
    string(REGEX REPLACE "^cc$" "c++" _bloc_cxx_name "${_bloc_cxx_name}")
    if(EXISTS "${_bloc_cc_dir}/${_bloc_cxx_name}")
      set(CMAKE_CXX_COMPILER "${_bloc_cc_dir}/${_bloc_cxx_name}")
    endif()
  endif()
  include(CheckLanguage)
  check_language(CXX)
endif()
if(CMAKE_CXX_COMPILER)
  enable_language(CXX)
  add_executable(test_cpp "${CMAKE_CURRENT_SOURCE_DIR}/test_cpp.cpp"
                          "${CMAKE_CURRENT_SOURCE_DIR}/support/ts_output.c")
  # bloc_test_support provides the assertion and lock hooks of the test configurations.
  target_link_libraries(test_cpp PRIVATE bloc::bloc bloc_test_support unity)
  set_target_properties(test_cpp PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED ON
                        CXX_EXTENSIONS OFF)
  target_compile_options(test_cpp PRIVATE
    "$<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>"
    "$<$<AND:$<BOOL:${BLOC_WERROR}>,$<CXX_COMPILER_ID:GNU,Clang>>:-Werror>")
  bloc_instrument(test_cpp)
  add_test(NAME test_cpp COMMAND test_cpp)
else()
  message(STATUS "No C++ compiler found: CFG-12 (test_cpp) is not built")
endif()
