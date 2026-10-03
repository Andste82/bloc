# Coverage, sanitizer and LTO flags for BLOC's own targets (top-level builds only).
# Everything is applied per target and PRIVATE, so none of it reaches a consumer (CM-02, CM-03).

if(BLOC_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _bloc_ipo_ok OUTPUT _bloc_ipo_out LANGUAGES C)
  if(NOT _bloc_ipo_ok)
    message(FATAL_ERROR "BLOC_LTO is ON but interprocedural optimization is not supported: ${_bloc_ipo_out}")
  endif()
endif()

if(BLOC_COVERAGE AND NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
  message(FATAL_ERROR "BLOC_COVERAGE requires GCC")
endif()

# bloc_instrument(<target> [NO_COVERAGE] [NO_LTO])
function(bloc_instrument target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "NO_COVERAGE;NO_LTO" "" "")

  if(BLOC_COVERAGE AND NOT arg_NO_COVERAGE)
    target_compile_options(${target} PRIVATE -O0 -g --coverage)
    target_link_options(${target} PRIVATE --coverage)
  endif()

  if(BLOC_SANITIZE)
    target_compile_options(${target} PRIVATE
      -fsanitize=${BLOC_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=${BLOC_SANITIZE})
  endif()

  if(BLOC_LTO AND NOT arg_NO_LTO)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
    target_compile_options(${target} PRIVATE -O3 -fstrict-aliasing)
    if(BLOC_WERROR)
      # The real compilation happens at link time, so warnings from it (for example
      # -Wlto-type-mismatch) are only turned into errors by a link option (LTO-02).
      if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_link_options(${target} PRIVATE -Werror)
      elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        target_link_options(${target} PRIVATE LINKER:--fatal-warnings)
      endif()
    endif()
  endif()
endfunction()
