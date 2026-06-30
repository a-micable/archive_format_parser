cmake_minimum_required(VERSION 3.16)
project(vector_archive LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vector_archive_lib
    src/byte_io.cc
    src/catalog.cc
    src/diagnostics.cc
    src/header.cc
    src/index.cc
    src/chunk.cc
    src/nested.cc
    src/pool.cc
    src/checksum.cc
    src/path_utils.cc
    src/path_report.cc
    src/format_rules.cc
    src/manifest.cc
    src/archive_stats.cc
    src/inspect.cc
    src/report.cc
    src/validator.cc
)

target_include_directories(vector_archive_lib PUBLIC src)

add_executable(vector src/main.cc)
target_link_libraries(vector PRIVATE vector_archive_lib)

enable_testing()

add_executable(pack_unpack_test tests/pack_unpack_test.cc)
target_link_libraries(pack_unpack_test PRIVATE vector_archive_lib)
add_test(NAME pack_unpack_test COMMAND pack_unpack_test)

add_executable(archive_tools_test tests/archive_tools_test.cc)
target_link_libraries(archive_tools_test PRIVATE vector_archive_lib)
add_test(NAME archive_tools_test COMMAND archive_tools_test)
