cmake_minimum_required(VERSION 3.16)

project(journal_collector
    VERSION 0.1
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)
find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)
find_package(Protobuf REQUIRED)
find_package(cppzmq REQUIRED)

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
    src/loop.cpp
    src/system_metrics.cpp
    src/adapter.cpp
)

target_include_directories(journal_collector_lib PUBLIC
    ${SYSTEMD_INCLUDE_DIRS}
    include/
)

target_link_libraries(journal_collector_lib PUBLIC
    ${SYSTEMD_LIBRARIES}
    yaml-cpp
    spdlog::spdlog
    proto_lib
    cppzmq
)

target_compile_options(journal_collector_lib PUBLIC
    ${SYSTEMD_CFLAGS_OTHER}
)

add_executable(journal_collector src/main.cpp)

target_link_libraries(journal_collector PRIVATE journal_collector_lib)

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