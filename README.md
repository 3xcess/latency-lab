# latency-lab

A real world market and low latency trading systems simulation.

Built in C++20.

## Architecture Overview

### Phase 1: Single-Thread Baseline

```
Market Data Generator
         ↓
    [Message Stream]
         ↓
    Order Book Engine
         ↓
    Metrics Collector
```

**Components:**

1. **MarketMessage** - Fixed-size, cache-aligned binary message struct
   - No dynamic allocation in the message itself
   - 64-byte alignment (one cache line) for optimal cache behavior

2. **MarketDataGenerator** - Deterministic pseudo-random message generator
   - Pre-generates all messages before processing
   - Separates generation cost from order book processing cost

3. **OrderBook** - limit order book
   - Uses `std::map` for price levels
   - Uses `std::unordered_map` for O(1) order lookup by ID
   - Handles: Add, Modify, Cancel, Trade operations

4. **LatencyRecorder** - Tail latency measurement
   - Records per-message latencies in nanoseconds
   - Computes percentiles: min, p50, p90, p99, p99.9, max

### Future Phases (Phase 2+)

- **Producer-consumer pipeline** with thread pinning
- **Lock-free SPSC ring buffer** queue implementation
- **False sharing** experiments with and without cache-line padding
- **Memory pool / arena allocator** experiments
- **TCP/UDP feed handlers** with socket tuning
- **Batching** strategy comparisons
- **Branch prediction** and prefetch experiments

## Build Instructions

### Prerequisites

- C++20 capable compiler (GCC 10+, Clang 10+, or MSVC 2019+)
- CMake 3.20+
- Linux or WSL for best results (Windows builds work but lack some Linux-specific features)

### Build Steps

```bash
cd latency-lab
mkdir -p build
cd build

# Configure the project
# For Release (optimized) build:
cmake -DCMAKE_BUILD_TYPE=Release ..

# For Debug build:
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Optional: Enable sanitizers for Debug builds
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON ..

# Build
cmake --build . -j $(nproc)
```

### Build Output

After successful build, you'll find:
- `benchmark_single_thread` - Single-threaded benchmark binary
- `benchmark_producer_consumer` - Producer-consumer benchmark binary
- `test_order_book` - Unit tests for order book correctness

## Running Benchmarks

### Single-Thread Baseline

Measures per-message latency through the order book in a single thread.

```bash
# Run with default 1M messages
./benchmark_single_thread

# Run with custom message count
./benchmark_single_thread 10000000
```

### Producer-Consumer Pipeline

Runs one producer thread that publishes messages into a bounded mutex queue and one consumer thread that updates the order book.

```bash
# Run with default 1M messages and 65,536 queue slots
./benchmark_producer_consumer

# Run with custom message count and queue capacity
./benchmark_producer_consumer 1000000 65536

# On Linux, optionally pin producer to CPU 2 and consumer to CPU 3
./benchmark_producer_consumer 1000000 65536 2 3
```

Producer-consumer latency measures time from producer enqueue timestamp to the end of order book processing. It includes queueing delay, wakeup cost, and consumer processing.

**Example Output:**

```
=== Single-Thread Order Book Benchmark ===
Generating 1000000 messages...
Generated 1000000 messages.
Warm-up complete.

=== Benchmark Results ===
Total Messages: 1000000
Total Time: 0.42 seconds
Throughput: 2.38 M msg/sec

=== Latency Statistics ===
Samples: 1000000
Min:     62 ns
P50:     112 ns
P90:     156 ns
P99:     412 ns
P99.9:   1.2 us
Max:     145 us
Mean:    118 ns
========================

=== Final Order Book State ===
Total Orders in Book: 3847
Buy Side Levels: 98
Sell Side Levels: 97
Best Bid: 15421 x 23456
Best Ask: 15422 x 18921
```

### Running Tests

```bash
# Run all tests
ctest --verbose

# Or run the test binary directly
./test_order_book
```

**Example Output:**

```
=== Order Book Unit Tests ===

Test: add_buy_order... PASS
Test: add_sell_order... PASS
Test: best_bid_updates... PASS
Test: cancel_order... PASS
Test: modify_order... PASS
Test: trade_partial_fill... PASS
Test: trade_full_fill... PASS
Test: multiple_orders_same_price... PASS

All tests passed!
```

## Design Decisions

### 1. MarketMessage Structure

Fixed 64-byte, cache-line-aligned POD struct with no dynamic allocation.
**Why:**
- **Cache efficiency**: One cache line = better prefetch and memory bandwidth utilization
- **No allocations**: Predictable memory layout; no allocation overhead
- **Zero-cost abstraction**: Compiler can inline and optimize aggressively
- **SIMD-friendly**: Aligned data works well with vectorization


### 2. Order Book Data Structures

`std::map` for price levels + `std::unordered_map` for order lookup.
**Why (MVP stage):**
- **std::map**: Automatic sorting by price; best_bid/best_ask are O(1) to fetch
- **std::unordered_map**: O(1) average lookup by order_id for modifications/cancels
- **Performance**: Micro-level latency is dominated by lock free algorithm, not container choice

**Future optimization:** Compare with:
- Flat/vector-based price levels (better cache locality for skewed distributions)
- Segment trees or specialized trading-system structures
- Object pools to reduce allocation frequency

### 3. Message Generation

Pre generate all messages before processing.
**Why:**
- **Accuracy**: Measures only order book processing
- **Realistic distribution**: 70% ADD, 15% MODIFY, 10% CANCEL, 5% TRADE

### 4. Latency Measurement

Record all per-message latencies; compute percentiles post-run.
**Why:**
- **Simple**: Sort once at the end (O(n log n))
- **Accurate**: No approximation or bucket quantization

### 5. Error Handling Philosophy

No exceptions in the hot path; silent ignore for invalid operations (ADD).
**Why:**
- **Latency**: Exceptions have non-zero cost (RTTI, unwinding)
- **Deterministic**: No hidden exception handling paths
- **Real-world mimicry**: Lost orders are logged/monitored, not exceptional

## Roadmap

### Phase 2: Producer-Consumer Pipeline
- [x] Mutex-based queue implementation
- [x] Producer and consumer threads
- [x] Basic CPU affinity helpers
- [x] Latency comparison: single-thread vs pipeline

### Phase 3: Lock-Free Patterns
- [ ] SPSC (Single-Producer Single-Consumer) bounded ring buffer
- [ ] Compare mutex queue vs lock-free queue
- [ ] False-sharing experiment with padded vs unpadded structures

### Phase 4: Memory Optimization
- [ ] Object pool / arena allocator
- [ ] Pre-allocation experiments
- [ ] Compare allocator behavior on latency

### Phase 5: Network Feed Handler
- [ ] TCP feed parser
- [ ] UDP feed parser
- [ ] Socket tuning: TCP_NODELAY, buffer sizes
- [ ] Epoll and async I/O patterns

### Phase 6: Advanced Topics
- [ ] Branch prediction and [[likely]] / [[unlikely]]
- [ ] Prefetch experiments with `__builtin_prefetch`
- [ ] NUMA awareness (multi-socket systems)
- [ ] Perf integration and flame graph generation

## Project Structure

```
latency-lab/
├── CMakeLists.txt              # Build configuration
├── README.md                   # This file
├── include/latency_lab/
│   ├── market_message.hpp      # Message struct and enums
│   ├── generator.hpp           # Message generator
│   ├── order_book.hpp          # Order book interface
│   ├── metrics.hpp             # Latency recorder
│   ├── mutex_queue.hpp         # Bounded mutex queue
│   └── thread_utils.hpp        # CPU affinity helpers
├── src/
│   ├── generator.cpp           # Generator implementation
│   ├── order_book.cpp          # Order book implementation
│   ├── metrics.cpp             # Metrics implementation
│   └── thread_utils.cpp        # Thread utility implementation
├── apps/
│   ├── benchmark_single_thread.cpp       # Single-thread benchmark
│   └── benchmark_producer_consumer.cpp   # Producer-consumer benchmark
├── tests/
│   └── test_order_book.cpp     # Order book unit tests
├── docs/
│   ├── architecture.md         # Detailed architecture
│   └── performance_notes.md    # Performance concepts
└── scripts/
```

## Performance Notes

### Why Tail Latency Matters

The worst-case latency determines profitability:
- A 100 µs delay on average is good
- A 10 ms spike (p99.9) can miss the market move
- Algorithms often fail not on average cases, but on tail events

### Why Allocations Matter

Dynamic allocations in the hot path can cause:
- Cache misses (no locality)
- Lock contention (global allocator)
- Garbage collector stalls (in other languages)
- Unpredictable latency (virtual memory paging)

Solution: Pre-allocate or use object pools.

### Why False Sharing Matters

Two threads accessing different data on the same cache line:
- Core 1 modifies byte 0 → cache line invalidated everywhere
- Core 2 misses on byte 64 (same line, not accessed) → stall

Solution: Align data to cache lines; pad structures to avoid hot-spots.

### Why CPU Pinning Helps

Without pinning:
- OS scheduler moves threads between cores
- L3 cache is not reused (warm cache lost)
- Memory topology (NUMA) is ignored
- Tail latencies spike during scheduling decisions

Solution: Pin producer and consumer to dedicated cores.

## Building on Windows vs. Linux

**Windows:** This project builds on Windows with MSVC or Clang, but some features are best on Linux:
- Linux perf profiling (not available on Windows)
- CPU affinity via `pthread_setaffinity_np` (requires WSL or custom code)
- Socket tuning via `TCP_NODELAY`, epoll, etc.

**Recommendation:** Use WSL2 or native Linux for best results.

## License

This project is provided as-is for educational purposes.

---

**Last Updated:** Phase 2 Complete

**Status:** Ready for Phase 3
