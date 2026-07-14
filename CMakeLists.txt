cmake_minimum_required(VERSION 3.16)
project(optical_netconf_roadm_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ONSIM_COVERAGE "Build with gcov coverage instrumentation" OFF)

add_library(onsim
  src/roadm.cpp
  src/transponder.cpp
  src/hal.cpp
)
target_include_directories(onsim PUBLIC include)
target_compile_options(onsim PRIVATE -Wall -Wextra -Wpedantic)

if(ONSIM_COVERAGE)
  target_compile_options(onsim PRIVATE --coverage -O0 -g)
  target_link_options(onsim PUBLIC --coverage)
endif()

# --- tests ---
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(onsim_tests
  tests/test_roadm.cpp
  tests/test_transponder.cpp
  tests/test_hal.cpp
)
target_link_libraries(onsim_tests PRIVATE onsim gtest_main)
target_compile_options(onsim_tests PRIVATE -Wall -Wextra)

include(GoogleTest)
gtest_discover_tests(onsim_tests)
