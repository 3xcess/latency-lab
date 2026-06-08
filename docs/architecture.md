# Architecture Overview

## Phase 1: Single-Thread Baseline

This document describes the detailed architecture of the Phase 1 implementation.

### System Flow

```
┌──────────────────┐
│ Market Generator │  Pseudo-random message generation
└────────┬─────────┘
         │
         ├─ Deterministic LCG (Linear Congruential Generator)
         ├─ Realistic distribution: 70% ADD, 15% MODIFY, 10% CANCEL, 5% TRADE
         └─ Pre-generates all messages to isolate generator cost
         │
         ↓
    ┌────────────┐
    │  Messages  │  Vector of fixed-size MarketMessage structs (64 bytes each)
    │  (in-mem)  │  Cache-aligned, no pointers, no allocations
    └────────────┘
         │
         ↓
    ┌──────────────┐
    │  Order Book  │  Processes each message:
    │   Engine     │   - ADD: new order
    │              │   - MODIFY: update quantity
    │              │   - CANCEL: remove order
    │              │   - TRADE: reduce quantity or remove
    └──────────────┘
         │
         ├─ Bids:   std map (descending sort)
         ├─ Asks:   std map (ascending sort)
         └─ Orders: std unordered_map
         │
         ↓
    ┌──────────────┐
    │   Metrics    │  Per-message latency recording
    │  Collector   │  Stores all latency samples for percentile computation
    └──────────────┘
         │
         ↓
    ┌──────────────┐
    │   Report     │
    │              │  - Throughput (msg/sec)
    │              │  - Min, p50, p90, p99, p99.9, max latency
    │              │  - Final order book state
    └──────────────┘
```

## Component Details

### 1. MarketMessage

**File:** `include/latency_lab/market_message.hpp`

```cpp
struct alignas(64) MarketMessage {
    std::uint64_t sequence_number;
    std::uint64_t exchange_timestamp_ns;
    std::uint64_t order_id;
    std::int64_t price;
    std::uint32_t quantity;
    Side side;                  // Buy = 0, Sell = 1
    MessageType type;           // Add, Modify, Cancel, Trade
    char symbol[8];
    std::uint8_t _padding[14];  // Pad to exactly 64 bytes
};
```

**Rationale:**
- **Cache alignment**: One cache line ensures:
  - Prefetch efficiency
  - No false sharing between messages
  - Simple to reason about memory layout
- **No pointers**: All inline data = no indirection
- **POD type**: trivial & can use memcpy
- **Compile-time verification**: `static_assert`

**Usage:**
```cpp
// Increment timestamp to avoid ties
msg.exchange_timestamp_ns = get_timestamp_ns();
// Message generation fills all fields deterministically
// No runtime allocation needed
```

### 2. MarketDataGenerator

**File:** `include/latency_lab/generator.hpp`, `src/generator.cpp`

**Algorithm:**
```
LCG (Linear Congruential Generator):
  seed = (seed * a + c) & mask
  
Message generation loop:
  1. Increment sequence_number
  2. Set timestamp to current time
  3. Cycle order_id through range 1000..11000 (reuse for MODIFY/CANCEL)
  4. Random price: 10000 + random() % 19000
  5. Random quantity: 100 + random() % 10000
  6. Random side: Buy or Sell (50/50)
  7. Random type: Add (70%), Modify (15%), Cancel (10%), Trade (5%)
  8. Append to vector
```

**Why**
- Separates generator overhead from order book processing
- Reproducible benchmarks

**Memory**
- 1M messages × 64 bytes = 64 MB
- All in main mem = good cache locality

### 3. OrderBook

**File:** `include/latency_lab/order_book.hpp`, `src/order_book.cpp`

**Data Structures:**

```cpp
std::map<std::int64_t, PriceLevel, std::greater<>>  bids_;  // Descending (best bid first)
std::map<std::int64_t, PriceLevel, std::less<>>     asks_;  // Ascending (best ask first)

std::unordered_map<std::uint64_t, Order>  order_map_;
```

**Operation Complexity:**
| Operation | Complexity | Notes |
|-----------|-----------|-------|
| ADD       | O(log P)  | P = number of price levels |
| MODIFY    | O(1)      | Order lookup is O(1); price level update is O(1) |
| CANCEL    | O(1)      | Lookup + removal from vector |
| TRADE     | O(1)      | Lookup + quantity update |
| best_bid  | O(1)      | First element in descending map |
| best_ask  | O(1)      | First element in ascending map |

- Most operations are O(1) amortized
- ADD is O(log P), but P is typically small (~100 price levels)
- No allocation in the hot path for MODIFY/CANCEL/TRADE

### 4. LatencyRecorder

**File:** `include/latency_lab/metrics.hpp`, `src/metrics.cpp`

**Algorithm:**
```
record(ns):
  samples_.push_back(ns)  // O(1) amortized

print_summary():
  1. Sort samples  // O(n log n) once
  2. Compute percentiles (linear interpolation)
  3. Print formatted output
```

**Output Example:**
```
=== Latency Statistics ===
Samples: 1000000
Min:     62 ns
P50:     112 ns
P90:     156 ns
P99:     412 ns
P99.9:   1.2 us
Max:     145 us
Mean:    118 ns
```

## Benchmark Execution Flow

```cpp
// benchmark_single_thread.cpp

int main() {
    MarketDataGenerator gen(1'000'000, "AAPL");
    gen.generate();
    
    OrderBook book;
    LatencyRecorder latency;
    
    // Warm-up (1000 messages, not measured)
    for (i = 0; i < 1000; ++i) {
        book.on_message(messages[i]);
    }
 
    for (const auto& msg : messages) {
        auto start = steady_clock::now();
        book.on_message(msg);
        auto end = steady_clock::now();
        latency.record(duration_cast<ns>(end - start).count());
    }

    latency.print_summary();
    book.best_bid();
    book.best_ask();
}
```

## Memory Layout and Cache Behavior

### MarketMessage in Cache

```
One MarketMessage = 64 bytes = one L1/L2 cache line

Memory Layout:
  Offset  Content                  Size (bytes)
  ------  -------                  -----------
  0       sequence_number          8
  8       exchange_timestamp_ns    8
  16      order_id                 8
  24      price                    8
  32      quantity                 4
  36      side                     1
  37      type                     1
  38      symbol[8]                8
  46      (padding)                18
  ------
  64 bytes total (one cache line)
```

### Cache Benefit

When processing messages sequentially the CPU prefetcher recognizes the sequential access and pre-loads the next cache line while processing the current one. 

Result: **~1 cache miss per message** instead of multiple misses if data were fragmented.

### OrderBook Memory Locality

**Bids/Asks maps:**
- std::map → scattered memory
- But only traversed at start (best_bid/best_ask)
- Update operations are on order lookup

**Order map:**
- Hash table → scattered memory
- Only accessed once per MODIFY/CANCEL/TRADE
- Single lookup is still < 100 ns

## Phase 2: Producer-Consumer Pipeline

Phase 2 adds a second benchmark path that separates market data publishing from order book processing.

```
Market Data Generator
        |
        v
Producer Thread
        |
        v
Bounded Mutex Queue
        |
        v
Consumer Thread
        |
        v
Order Book Engine
        |
        v
Latency Recorder
```

### New Components

**BoundedMutexQueue**

File: `include/latency_lab/mutex_queue.hpp`

- Uses `std::mutex` and `std::condition_variable`
- Applies backpressure when the queue reaches capacity
- Provides the correctness baseline before lock-free experiments

**Thread utilities**

Files: `include/latency_lab/thread_utils.hpp`, `src/thread_utils.cpp`

- `pin_thread_to_cpu(int cpu_id)` uses `pthread_setaffinity_np` on Linux
- `get_current_cpu()` uses `sched_getcpu` on Linux
- Non-Linux builds return `false` or `-1`, so the project remains portable

### Benchmark Semantics

The producer-consumer benchmark records latency from producer enqueue timestamp to completion of order book processing in the consumer. This intentionally includes queueing delay and scheduler wakeup overhead, unlike the single-thread benchmark, which isolates the order book processing path.

## Phase 3: SPSC Queue, False Sharing, and Batching

Phase 3 adds focused experiments for common low-latency engineering tradeoffs.

### SPSC Ring Buffer Pipeline

```
Market Data Generator
        |
        v
Producer Thread
        |
        v
SPSC Ring Buffer
        |
        v
Consumer Thread
        |
        v
Order Book Engine
        |
        v
Latency Recorder
```

**SpscRingBuffer**

File: `include/latency_lab/spsc_ring_buffer.hpp`

- Uses one producer and one consumer
- Preallocates bounded storage during construction
- Keeps producer-owned and consumer-owned indices on separate cache lines
- Returns `false` on full or empty states so the benchmark controls spin/yield policy

### False Sharing

File: `apps/benchmark_false_sharing.cpp`

- Runs two threads incrementing independent atomic counters
- Compares adjacent counters against cache-line padded counters
- Reports throughput and CPU migration counts

### Batching

File: `apps/benchmark_batching.cpp`

- Runs the order book with batch sizes 1, 8, 32, and 64
- Reports throughput and amortized per-message batch processing latency
- Demonstrates that batching can improve throughput while changing latency semantics

## Future Optimizations (Phase 4+)

- Object Pool
- Segment Tree
- SIMD Processing
- NUMA Awareness

---

**Status:** Phase 3 complete
