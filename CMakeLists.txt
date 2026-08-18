cmake_minimum_required(VERSION 3.16)
project(pickpoint_nano VERSION 2.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(PICKPOINT_NANO_BUILD_TESTS "Build unit tests" ON)

add_library(pickpoint_nano src/pickpoint_nano.c)
add_library(pickpoint_nano::pickpoint_nano ALIAS pickpoint_nano)

target_include_directories(pickpoint_nano
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
  target_compile_options(pickpoint_nano PRIVATE /W4)
else()
  target_compile_options(pickpoint_nano PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(TARGETS pickpoint_nano
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES include/pickpoint_nano.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(PICKPOINT_NANO_BUILD_TESTS)
  enable_testing()
  add_executable(pickpoint_nano_tests tests/test_nano.c)
  target_link_libraries(pickpoint_nano_tests PRIVATE pickpoint_nano)
  if(NOT MSVC)
    target_compile_options(pickpoint_nano_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME pickpoint_nano_tests COMMAND pickpoint_nano_tests)
endif()
