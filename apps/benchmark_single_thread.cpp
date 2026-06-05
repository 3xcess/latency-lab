#include "latency_lab/market_message.hpp"
#include "latency_lab/generator.hpp"
#include "latency_lab/order_book.hpp"
#include "latency_lab/metrics.hpp"
#include <iostream>
#include <chrono>
#include <cstdlib>

// Single-threaded benchmark: measure order book processing latency.
// Decision: Single-threaded to isolate order book performance from concurrency overhead.
int main(int argc, char* argv[]) {
    using namespace latency_lab;
    using namespace std::chrono;

    // Configuration
    std::uint64_t num_messages = 1'000'000;
    if (argc > 1) {
        num_messages = std::stoull(argv[1]);
    }

    std::cout << "=== Single-Thread Order Book Benchmark ===\n";
    std::cout << "Generating " << num_messages << " messages...\n";

    // generate msgs
    MarketDataGenerator generator(num_messages, "AAPL");
    generator.generate();
    const auto& messages = generator.messages();

    std::cout << "Generated " << messages.size() << " messages.\n";

    // process messages and measure latency
    OrderBook book;
    LatencyRecorder latency_recorder;

    // warm-up with a few messages without measuring.
    // helps CPU caches and branch predictors settle, improves consistency.
    constexpr std::uint64_t warmup_count = 1000;
    for (std::uint64_t i = 0; i < std::min(warmup_count, static_cast<std::uint64_t>(messages.size())); ++i) {
        book.on_message(messages[i]);
    }
    std::cout << "Warm-up complete.\n";

    // process all messages and record per message latency.
    auto benchmark_start = steady_clock::now();
    for (const auto& msg : messages) {
        // time to process a single message.
        auto msg_start = steady_clock::now();
        book.on_message(msg);
        auto msg_end = steady_clock::now();

        auto latency_ns = duration_cast<nanoseconds>(msg_end - msg_start).count();
        latency_recorder.record(static_cast<std::uint64_t>(latency_ns));
    }
    auto benchmark_end = steady_clock::now();

    // throughput
    auto total_duration_ns = duration_cast<nanoseconds>(benchmark_end - benchmark_start).count();
    double throughput_msg_per_sec = (static_cast<double>(num_messages) / total_duration_ns) * 1e9;

    std::cout << "\n=== Benchmark Results ===\n";
    std::cout << "Total Messages: " << num_messages << "\n";
    std::cout << "Total Time: " << (total_duration_ns / 1e9) << " seconds\n";
    std::cout << "Throughput: " << (throughput_msg_per_sec / 1e6) << " M msg/sec\n";

    //per-message latency statistics
    latency_recorder.print_summary();

    std::cout << "=== Final Order Book State ===\n";
    std::cout << "Total Orders in Book: " << book.total_orders() << "\n";
    std::cout << "Buy Side Levels: " << book.buy_side_levels() << "\n";
    std::cout << "Sell Side Levels: " << book.sell_side_levels() << "\n";

    if (auto best_bid = book.best_bid()) {
        std::cout << "Best Bid: " << best_bid->price << " x " << best_bid->total_quantity << "\n";
    }
    if (auto best_ask = book.best_ask()) {
        std::cout << "Best Ask: " << best_ask->price << " x " << best_ask->total_quantity << "\n";
    }

    return 0;
}
