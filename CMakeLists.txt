cmake_minimum_required(VERSION 3.16)
project(optical_netconf_roadm_sim C CXX)

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

# --- experiment tool: BER-vs-OSNR sweep of the transponder model ---
add_executable(onsim-ber-sweep tools/ber_sweep.cpp)
target_link_libraries(onsim-ber-sweep PRIVATE onsim)
target_compile_options(onsim-ber-sweep PRIVATE -Wall -Wextra)

# --- NETCONF management daemon (needs libsysrepo; built in the container) ---
option(ONSIM_NETCONF "Build the NE daemons (management plane + device daemon)" OFF)
if(ONSIM_NETCONF)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SYSREPO REQUIRED IMPORTED_TARGET sysrepo)
  pkg_check_modules(LIBYANG REQUIRED IMPORTED_TARGET libyang)
  pkg_check_modules(ZMQ REQUIRED IMPORTED_TARGET libzmq)
  find_package(Protobuf REQUIRED)
  find_package(CycloneDDS REQUIRED)

  # protobuf telemetry schema (shared by both daemons and the subscriber)
  protobuf_generate_cpp(TELEM_SRCS TELEM_HDRS proto/onsim_telemetry.proto)
  add_library(onsim_telemetry_pb STATIC ${TELEM_SRCS})
  target_include_directories(onsim_telemetry_pb PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${Protobuf_INCLUDE_DIRS})
  target_link_libraries(onsim_telemetry_pb PUBLIC ${Protobuf_LIBRARIES})

  # DDS types for the internal bus (control commands + telemetry blob)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/onsim_dds.c ${CMAKE_CURRENT_BINARY_DIR}/onsim_dds.h
    COMMAND CycloneDDS::idlc -l c ${CMAKE_CURRENT_SOURCE_DIR}/idl/onsim_dds.idl
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS idl/onsim_dds.idl)
  add_library(onsim_dds_types STATIC ${CMAKE_CURRENT_BINARY_DIR}/onsim_dds.c)
  set_target_properties(onsim_dds_types PROPERTIES LINKER_LANGUAGE C)
  target_include_directories(onsim_dds_types PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(onsim_dds_types PUBLIC CycloneDDS::ddsc)

  # device daemon: owns the HAL, serves the DDS control bus, publishes telemetry
  add_executable(onsim-devd netconf/onsim_devd.cpp)
  target_include_directories(onsim-devd PRIVATE netconf)
  target_link_libraries(onsim-devd PRIVATE onsim onsim_dds_types onsim_telemetry_pb PkgConfig::ZMQ)
  target_compile_options(onsim-devd PRIVATE -Wall -Wextra)

  # management plane: sysrepo/NETCONF <-> DDS bus (never touches the HAL)
  add_executable(onsim-netconfd netconf/onsim_netconfd.cpp)
  target_include_directories(onsim-netconfd PRIVATE netconf)
  target_link_libraries(onsim-netconfd PRIVATE PkgConfig::SYSREPO PkgConfig::LIBYANG onsim_dds_types onsim_telemetry_pb)
  target_compile_options(onsim-netconfd PRIVATE -Wall -Wextra)

  # external DDS telemetry subscriber (demo / debugging)
  add_executable(onsim-dds-sub tools/dds_sub.cpp)
  target_include_directories(onsim-dds-sub PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(onsim-dds-sub PRIVATE onsim_dds_types onsim_telemetry_pb)

  install(TARGETS onsim-devd onsim-netconfd onsim-dds-sub RUNTIME DESTINATION bin)
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
