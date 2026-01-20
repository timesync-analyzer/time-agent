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

add_executable(journal_collector
    src/collector.cpp
    src/config.cpp
    src/loop.cpp
    src/metrics.cpp
    src/main.cpp
)

target_include_directories(journal_collector PRIVATE
    ${SYSTEMD_INCLUDE_DIRS}
    include/
)

target_link_libraries(journal_collector PRIVATE
    ${SYSTEMD_LIBRARIES}
    yaml-cpp
    spdlog::spdlog
)

target_compile_options(journal_collector PRIVATE
    ${SYSTEMD_CFLAGS_OTHER}
)