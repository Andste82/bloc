# Examples are added in phase 7; until basic.c exists this directory builds nothing.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/basic.c")
  add_executable(bloc_example_basic basic.c)
  target_link_libraries(bloc_example_basic PRIVATE bloc::bloc)
endif()
