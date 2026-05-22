cmake_minimum_required(VERSION 3.20)
project(lob LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(LOB_NATIVE "Build with -march=native" ON)
option(LOB_BENCH "Build benchmarks" ON)
option(LOB_TESTS "Build tests" ON)
set(LOB_SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. address,undefined or thread")

add_compile_options(-Wall -Wextra -Wpedantic)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O3)
  if(LOB_NATIVE)
    add_compile_options(-march=native)
  endif()
endif()
if(LOB_SANITIZE)
  add_compile_options(-fsanitize=${LOB_SANITIZE} -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${LOB_SANITIZE})
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

add_library(lob src/order_book.cpp src/matching_engine.cpp src/messages.cpp src/workload.cpp)
target_include_directories(lob PUBLIC include)
target_compile_definitions(lob PUBLIC $<$<CONFIG:Debug>:LOB_CHECK_INVARIANTS>)

add_library(lob_reference bench/reference_book.cpp)
target_include_directories(lob_reference PUBLIC bench)
target_link_libraries(lob_reference PUBLIC lob)

add_executable(gen_workload apps/gen_workload.cpp)
target_link_libraries(gen_workload PRIVATE lob)

add_executable(replay apps/replay.cpp)
target_link_libraries(replay PRIVATE lob lob_reference)
find_package(Threads REQUIRED)
target_link_libraries(replay PRIVATE Threads::Threads)

if(LOB_BENCH)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(HDR_HISTOGRAM_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(HDR_HISTOGRAM_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(HDR_LOG_REQUIRED OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG c58e6d0710581e3a08d65c349664128a8d9a2461)  # v1.9.1
  FetchContent_Declare(hdr_histogram
    GIT_REPOSITORY https://github.com/HdrHistogram/HdrHistogram_c.git
    GIT_TAG 8dcce8f68512fca460b171bccc3a5afce0048779)  # 0.11.8
  FetchContent_MakeAvailable(benchmark hdr_histogram)

  add_executable(bench_book bench/bench_book.cpp)
  target_link_libraries(bench_book PRIVATE lob lob_reference benchmark::benchmark)

  add_executable(latency_harness bench/latency_harness.cpp)
  target_link_libraries(latency_harness PRIVATE lob lob_reference hdr_histogram_static Threads::Threads)
endif()

if(LOB_TESTS)
  enable_testing()
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG b514bdc898e2951020cbdca1304b75f5950d1f59)  # v1.15.2
  FetchContent_MakeAvailable(googletest)
  include(GoogleTest)

  add_executable(lob_tests tests/test_book.cpp tests/test_fuzz.cpp tests/test_spsc.cpp tests/test_pool.cpp)
  target_link_libraries(lob_tests PRIVATE lob lob_reference GTest::gtest_main Threads::Threads)
  gtest_discover_tests(lob_tests)

  add_executable(alloc_test tests/test_alloc.cpp)
  target_link_libraries(alloc_test PRIVATE lob GTest::gtest_main)
  gtest_discover_tests(alloc_test)
endif()
