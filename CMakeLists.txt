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

# --- NETCONF management daemon (needs libsysrepo; built in the container) ---
option(ONSIM_NETCONF "Build the sysrepo/NETCONF management daemon" OFF)
if(ONSIM_NETCONF)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SYSREPO REQUIRED IMPORTED_TARGET sysrepo)
  pkg_check_modules(LIBYANG REQUIRED IMPORTED_TARGET libyang)
  add_executable(onsim-netconfd netconf/onsim_netconfd.cpp)
  target_link_libraries(onsim-netconfd PRIVATE onsim PkgConfig::SYSREPO PkgConfig::LIBYANG)
  target_compile_options(onsim-netconfd PRIVATE -Wall -Wextra)

  option(ONSIM_TELEMETRY "Publish protobuf telemetry over ZeroMQ" ON)
  if(ONSIM_TELEMETRY)
    find_package(Protobuf REQUIRED)
    pkg_check_modules(ZMQ REQUIRED IMPORTED_TARGET libzmq)
    protobuf_generate_cpp(TELEM_SRCS TELEM_HDRS proto/onsim_telemetry.proto)
    add_library(onsim_telemetry_pb STATIC ${TELEM_SRCS})
    target_include_directories(onsim_telemetry_pb PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${Protobuf_INCLUDE_DIRS})
    target_link_libraries(onsim_telemetry_pb PUBLIC ${Protobuf_LIBRARIES})
    target_compile_definitions(onsim-netconfd PRIVATE ONSIM_TELEMETRY)
    target_include_directories(onsim-netconfd PRIVATE netconf)
    target_link_libraries(onsim-netconfd PRIVATE onsim_telemetry_pb PkgConfig::ZMQ)
  endif()

  install(TARGETS onsim-netconfd RUNTIME DESTINATION bin)
endif()

# --- tests ---
option(ONSIM_BUILD_TESTS "Build the GoogleTest unit tests" ON)
if(ONSIM_BUILD_TESTS)
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
endif()
