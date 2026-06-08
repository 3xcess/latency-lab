cmake_minimum_required(VERSION 3.20)
project(latency-lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native)
    endif()
endif()

if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(Threads REQUIRED)

set(CORE_SOURCES
    src/generator.cpp
    src/order_book.cpp
    src/metrics.cpp
    src/thread_utils.cpp
)

add_library(latency_lab_core STATIC ${CORE_SOURCES})
target_include_directories(latency_lab_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

set_target_properties(latency_lab_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(benchmark_single_thread apps/benchmark_single_thread.cpp)
target_link_libraries(benchmark_single_thread PRIVATE latency_lab_core)
target_include_directories(benchmark_single_thread PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(benchmark_producer_consumer apps/benchmark_producer_consumer.cpp)
target_link_libraries(benchmark_producer_consumer PRIVATE latency_lab_core Threads::Threads)
target_include_directories(benchmark_producer_consumer PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(benchmark_spsc_queue apps/benchmark_spsc_queue.cpp)
target_link_libraries(benchmark_spsc_queue PRIVATE latency_lab_core Threads::Threads)
target_include_directories(benchmark_spsc_queue PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(benchmark_false_sharing apps/benchmark_false_sharing.cpp)
target_link_libraries(benchmark_false_sharing PRIVATE latency_lab_core Threads::Threads)
target_include_directories(benchmark_false_sharing PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(benchmark_batching apps/benchmark_batching.cpp)
target_link_libraries(benchmark_batching PRIVATE latency_lab_core)
target_include_directories(benchmark_batching PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(test_order_book tests/test_order_book.cpp)
target_link_libraries(test_order_book PRIVATE latency_lab_core)
target_include_directories(test_order_book PRIVATE ${CMAKE_SOURCE_DIR}/include)

enable_testing()
add_test(NAME order_book_tests COMMAND test_order_book)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Sanitizers enabled: ${ENABLE_SANITIZERS}")
