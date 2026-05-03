cmake_minimum_required(VERSION 3.16)

project(journal_collector
    VERSION 0.1
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

option(TIME_AGENT_ENABLE_SANITIZERS "Build time-agent with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_compile_options(-Wall -Wextra -Wpedantic -fno-omit-frame-pointer)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -DNDEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g3 -DDEBUG)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)
pkg_check_modules(LIBPCI QUIET libpci)
find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(Protobuf REQUIRED)
find_package(cppzmq REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem)

find_path(LIBPCI_INCLUDE_DIR
    NAMES pci/pci.h
    HINTS ${LIBPCI_INCLUDE_DIRS}
)
find_library(LIBPCI_LIBRARY
    NAMES pci
    HINTS ${LIBPCI_LIBRARY_DIRS}
)

if(NOT LIBPCI_INCLUDE_DIR OR NOT LIBPCI_LIBRARY)
    message(FATAL_ERROR "libpci headers or library not found. Install libpci-dev (Debian/Ubuntu) or pciutils-devel (Fedora/RHEL).")
endif()

set(PROTO_FILES protocol/proto/metrics.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

add_library(proto_lib ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(proto_lib PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(proto_lib PUBLIC ${Protobuf_LIBRARIES})

add_library(journal_collector_lib
    src/collector.cpp
    src/config.cpp
    src/agent.cpp
    src/network_adapter_resolver.cpp
    src/system_metrics.cpp
    src/adapter.cpp
)

target_include_directories(journal_collector_lib PUBLIC
    ${SYSTEMD_INCLUDE_DIRS}
    ${LIBPCI_INCLUDE_DIR}
    include/
)

target_link_libraries(journal_collector_lib PUBLIC
    ${SYSTEMD_LIBRARIES}
    ${LIBPCI_LIBRARY}
    yaml-cpp
    spdlog::spdlog
    proto_lib
    cppzmq
)

target_compile_options(journal_collector_lib PUBLIC
    ${SYSTEMD_CFLAGS_OTHER}
    ${LIBPCI_CFLAGS_OTHER}
)

add_executable(time_agent src/main.cpp)

target_link_libraries(time_agent PRIVATE journal_collector_lib)

enable_testing()

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(tests
    tests/test_collector.cpp
    tests/test_config.cpp
)

target_link_libraries(tests PRIVATE
    journal_collector_lib
    GTest::gtest_main
    GTest::gmock
)

add_custom_command(TARGET tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/tests/fixtures
    $<TARGET_FILE_DIR:tests>/fixtures
)

include(GoogleTest)
gtest_discover_tests(tests)
