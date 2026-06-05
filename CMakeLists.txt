cmake_minimum_required(VERSION 3.20)
project(latency-lab LANGUAGES CXX)

# Enforce C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for strict warnings and optimization
# Decision: Enable strict warnings to catch issues early.
# We use -Wall -Wextra -Wpedantic for maximum visibility into potential problems.
if(MSVC)
    # MSVC compiler
    add_compile_options(/W4 /WX)
else()
    # GCC/Clang
    add_compile_options(-Wall -Wextra -Wpedantic)
    
    # Additional optimization flags for Release builds
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native)
    endif()
endif()

# Optional: Enable sanitizers for Debug builds
# Decision: Sanitizers help catch memory issues and undefined behavior during development.
# Users can enable with: cmake -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Add the include directory
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files for the core library
set(CORE_SOURCES
    src/generator.cpp
    src/order_book.cpp
    src/metrics.cpp
)

# Create a static library for core components
# Decision: A static library makes it easy to link with multiple test/benchmark binaries.
add_library(latency_lab_core STATIC ${CORE_SOURCES})
target_include_directories(latency_lab_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Enable position-independent code for potential future shared library use
set_target_properties(latency_lab_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Benchmark: Single-thread baseline
# Decision: Single-threaded benchmark isolates order book performance.
add_executable(benchmark_single_thread apps/benchmark_single_thread.cpp)
target_link_libraries(benchmark_single_thread PRIVATE latency_lab_core)
target_include_directories(benchmark_single_thread PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Tests: Order book correctness
# Decision: Assert-based tests for simplicity in MVP.
add_executable(test_order_book tests/test_order_book.cpp)
target_link_libraries(test_order_book PRIVATE latency_lab_core)
target_include_directories(test_order_book PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Enable testing
enable_testing()
add_test(NAME order_book_tests COMMAND test_order_book)

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Sanitizers enabled: ${ENABLE_SANITIZERS}")
