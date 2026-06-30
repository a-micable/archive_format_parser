#!/bin/bash -eu
$CXX $CXXFLAGS -I./src -c src/*.cc
$CXX $CXXFLAGS $LIB_FUZZING_ENGINE fuzz/unpack_fuzzer.cc *.o -o $OUT/unpack_fuzzer
$CXX $CXXFLAGS $LIB_FUZZING_ENGINE fuzz/nested_fuzzer.cc *.o -o $OUT/nested_fuzzer
