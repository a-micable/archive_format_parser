cmake_minimum_required(VERSION 3.16)
project(vector_archive LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vector_archive_lib
    src/header.cc
    src/index.cc
    src/chunk.cc
    src/nested.cc
    src/pool.cc
    src/checksum.cc
)

target_include_directories(vector_archive_lib PUBLIC src)

add_executable(vector src/main.cc)
target_link_libraries(vector PRIVATE vector_archive_lib)

enable_testing()

add_executable(pack_unpack_test tests/pack_unpack_test.cc)
target_link_libraries(pack_unpack_test PRIVATE vector_archive_lib)
add_test(NAME pack_unpack_test COMMAND pack_unpack_test)
