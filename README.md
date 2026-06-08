# latency-lab

A low latency trading systems simulation and performance engineering lab for market data ingestion, limit order book updates, and end-to-end latency benchmarking. 

Compares:
- single-threaded processing
- mutex queues
- SPSC ring buffers
- cache-line padding
- batching
- memory pools
- TCP/UDP feeds
- branch prediction
- prefetching
to study how real systems bottlenecks affect throughput and tail latency.

Built in C++20.


## Architecture Overview

### Phase 1: Single-Thread Baseline

```
Market Data Generator
         ||
         \/
    [Message Stream]
         ||
         \/
    Order Book Engine
         ||
         \/
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

### Phase 2: Producer-consumer pipeline:

Adds a second benchmark path that separates market data publishing from order book processing.

```
Market Data Generator
        ||
        \/
Producer Thread
        ||
        \/
Bounded Mutex Queue
        ||
        \/
Consumer Thread
        ||
        \/
Order Book Engine
        ||
        \/
Latency Recorder
```

**Additions:**

1. **BoundedMutexQueue** 
   - Mutex based queue implementation
   - Applies backpressure when the queue reaches capacity 

2. **Thread Pinning** 
   - ThreadUtils files to introduce thread pinning to the project
   - Separates market data publishing from order book processing
   - Pins producer and consumer workers to different threads

3. **producer-consumer Latency Recorder**
   - separates pipeline latency, queue residence time, and book processing latency
   - Results show that the additional latency comes primarily from queue residence time and thread handoff overhead

### Phase 3: Lock-free queue, false sharing, and batching

Adds experiments for comparing synchronization costs, cache-line contention, and batch-size tradeoffs.

**Additions:**

1. **SpscRingBuffer**
   - Bounded single-producer/single-consumer ring buffer
   - Preallocated storage and padded head/tail indices
   - Non-blocking `push`/`pop` API for explicit spin/yield policy

2. **False sharing benchmark**
   - Compares adjacent atomic counters against cache-line padded counters
   - Demonstrates how independent data can still contend through cache coherence

3. **Batching benchmark**
   - Compares batch sizes 1, 8, 32, and 64
   - Reports throughput and amortized per-message processing latency

### Phase 4: Memory pool and allocator experiment

Adds an order book variant backed by a preallocated object pool for `Order` storage.

**Additions:**

1. **ObjectPool**
   - Fixed-capacity pool with placement-new construction
   - Recycles freed slots without returning memory to the heap
   - Tracks pool allocations, deallocations, available slots, and exhaustion events

2. **PooledOrderBook**
   - Uses `ObjectPool<Order>` for order storage
   - Reserves the order lookup table up front
   - Keeps the same public book interface as the baseline benchmark path

3. **Memory pool benchmark**
   - Compares baseline `OrderBook` against `PooledOrderBook`
   - Reports throughput, tail latency, pool counters, and final book state

### Phase 5: TCP and UDP feed handlers

Adds loopback networking benchmarks that send fixed-size `MarketMessage` packets through Linux sockets.

**Additions:**

1. **Socket utilities**
   - Helpers for `TCP_NODELAY`, socket buffer sizes, nonblocking mode, and exact TCP send/receive
   - Linux implementation with portable stubs for unsupported platforms

2. **TCP feed benchmark**
   - Producer sends binary `MarketMessage` records over a loopback TCP connection
   - Consumer receives complete messages and applies them to `OrderBook`
   - Supports `TCP_NODELAY`, send/receive buffer sizes, and CPU pinning

3. **UDP feed benchmark**
   - Producer sends one `MarketMessage` per UDP datagram
   - Consumer reports received count and dropped/unreceived datagrams
   - Supports socket buffer sizes, receive timeout, and CPU pinning

### Phase 6: Fun stuff

**Additions:**

1. **Branch prediction benchmark**
   - Compares predictable and unpredictable message-type distributions
   - Runs dispatch with and without `[[likely]]` / `[[unlikely]]`
   - Demonstrates that branch hints can help or hurt depending on distribution and compiler output

2. **Prefetch benchmark**
   - Compares sequential message scans with prefetch distances 0, 4, 16, and 64
   - Uses `__builtin_prefetch` on GCC/Clang
   - Shows when software prefetch can improve or degrade throughput

### Future Experiments
tbd when I'll get to them...

- Flat/vector price-level order book
- NUMA-aware allocation and thread placement
- Maybe try to implement gap detection (for UDP and stuff)

### P.S. 
All results and notes regarding different phases in the docs folder are local development machine measurements and should be treated as comparative evidence of my personal runs, not universal hardware claims.

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

- `benchmark_single_thread` - Single-threaded benchmark binary
- `benchmark_producer_consumer` - Producer-consumer benchmark binary
- `benchmark_spsc_queue` - SPSC ring buffer producer-consumer benchmark binary
- `benchmark_spsc_queue_pooled` - SPSC ring buffer benchmark using pooled order book binary
- `benchmark_false_sharing` - Cache-line false sharing benchmark binary
- `benchmark_batching` - Batch-size comparison benchmark binary
- `benchmark_memory_pool` - Baseline vs pooled order book benchmark binary
- `benchmark_tcp_feed` - TCP loopback feed benchmark binary on Linux
- `benchmark_udp_feed` - UDP loopback feed benchmark binary on Linux
- `benchmark_branch_prediction` - Branch distribution and hint benchmark binary
- `benchmark_prefetch` - Software prefetch distance benchmark binary
- `test_order_book` - Unit tests for order book correctness
- `test_pooled_order_book` - Unit tests for pooled order book correctness

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

### SPSC Ring Buffer Pipeline

Runs the same producer-consumer order book pipeline using a bounded SPSC ring buffer instead of a mutex queue.

```bash
# Run with default 1M messages and 65,536 queue slots
./benchmark_spsc_queue

# Run with custom message count and queue capacity
./benchmark_spsc_queue 1000000 65536

# On Linux, optionally pin producer to CPU 2 and consumer to CPU 3
./benchmark_spsc_queue 1000000 65536 2 3
```

### SPSC Ring Buffer with Pooled Order Book

Runs the SPSC pipeline with `PooledOrderBook` instead of the baseline order book.

```bash
# Run with default 1M messages, 65,536 queue slots, and pool capacity equal to message count
./benchmark_spsc_queue_pooled

# Run with custom message count, queue capacity, CPU pins, and pool capacity
./benchmark_spsc_queue_pooled 1000000 65536 2 3 1000000
```

### False Sharing

Compares two threads incrementing adjacent atomics versus cache-line padded atomics.

```bash
# Run with default 100M increments per thread
./benchmark_false_sharing

# Run with custom iterations and optional CPU pinning
./benchmark_false_sharing 100000000 2 3
```

### Batching

Compares order book processing in batch sizes 1, 8, 32, and 64.

```bash
# Run with default 1M messages
./benchmark_batching

# Run with custom message count
./benchmark_batching 1000000
```

### Memory Pool

Compares the baseline order book against a pooled order book that preallocates order storage.

```bash
# Run with default 1M messages and pool capacity equal to message count
./benchmark_memory_pool

# Run with custom message count and pool capacity
./benchmark_memory_pool 1000000 1000000
```

### TCP Feed

Runs a loopback TCP feed where the producer writes binary `MarketMessage` records and the consumer applies them to the order book.

```bash
# Run with default 1M messages, TCP_NODELAY enabled, default socket buffers
./benchmark_tcp_feed

# Run with custom messages, TCP_NODELAY, send buffer, receive buffer, and CPU pins
./benchmark_tcp_feed 1000000 1 1048576 1048576 2 3
```

### UDP Feed

Runs a loopback UDP feed with one `MarketMessage` per datagram. UDP can drop messages, so the report includes dropped/unreceived count.

```bash
# Run with default 1M messages and default socket buffers
./benchmark_udp_feed

# Run with custom messages, send buffer, receive buffer, CPU pins, and receive timeout ms
./benchmark_udp_feed 1000000 1048576 1048576 2 3 1000
```

### Branch Prediction

Compares predictable and unpredictable message dispatch with and without branch hints.

```bash
# Run with default 5M messages
./benchmark_branch_prediction

# Run with custom message count
./benchmark_branch_prediction 5000000
```

### Prefetch

Compares message scanning with prefetch distances 0, 4, 16, and 64.

```bash
# Run with default 5M messages
./benchmark_prefetch

# Run with custom message count
./benchmark_prefetch 5000000
```

**Example Output:**

```
=== Single-Thread Order Book Benchmark ===
Generating 1000000 messages...
Generated 1000000 messages.
Warm-up complete.

=== Benchmark Results ===
Total Messages: 1000000
Total Time: 0.118879 seconds
Throughput: 8.4119 M msg/sec

=== Latency Statistics ===
Samples: 1000000
Min:     19 ns
P50:     40 ns
P90:     224 ns
P99:     324 ns
P99.9:   713 ns
Max:     213.62 us
Mean:    96 ns
========================

=== Final Order Book State ===
Total Orders in Book: 8516
Buy Side Levels: 3776
Sell Side Levels: 3858
Best Bid: 28997 x 4635
Best Ask: 10003 x 10051

```

### Running Tests

```bash

ctest --verbose
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
- [x] SPSC (Single-Producer Single-Consumer) bounded ring buffer
- [x] Compare mutex queue vs lock-free queue
- [x] False-sharing experiment with padded vs unpadded structures
- [x] Batching benchmark for batch sizes 1, 8, 32, and 64

### Phase 4: Memory Optimization
- [x] Object pool for order storage
- [x] Pre-allocation experiment with pooled order book
- [x] Compare allocator behavior on latency

### Phase 5: Network Feed Handler
- [x] TCP feed benchmark
- [x] UDP feed benchmark
- [x] Socket tuning: TCP_NODELAY and buffer sizes
- [ ] Epoll and async I/O patterns

### Phase 6: Fun stuff
- [x] Branch prediction and [[likely]] / [[unlikely]]
- [x] Prefetch experiments with `__builtin_prefetch`
- [ ] Perf integration and flame graph generation

## Project Structure

```
latency-lab/
├── CMakeLists.txt              # Build config
├── README.md                   
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
|
├── apps/                       # Benchmarks
|
├── tests/
│   └── test_order_book.cpp     # Order book unit tests
├── docs/
│   ├── architecture.md         # Detailed architecture
│   └── performance_notes.md    # Performance concepts
└── scripts/
```

## Building on Windows vs. Linux

**Windows:** This project builds on Windows with MSVC or Clang, but some features are best on Linux:
- Linux perf profiling (not available on Windows)
- CPU affinity via `pthread_setaffinity_np` (requires WSL or custom code)
- Socket tuning via `TCP_NODELAY`, epoll, etc.

**Recommendation:** Use WSL2 or native Linux for best results.

## License

This project is provided as-is for educational purposes.
