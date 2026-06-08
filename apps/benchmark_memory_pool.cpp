#include "latency_lab/generator.hpp"
#include "latency_lab/metrics.hpp"
#include "latency_lab/order_book.hpp"
#include "latency_lab/pooled_order_book.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::uint64_t parse_u64_arg(char* value, std::uint64_t fallback) {
    if (value == nullptr) {
        return fallback;
    }
    return std::stoull(value);
}

template <typename Book>
void print_final_book_state(const Book& book) {
    std::cout << "Total Orders in Book: " << book.total_orders() << "\n";
    std::cout << "Buy Side Levels: " << book.buy_side_levels() << "\n";
    std::cout << "Sell Side Levels: " << book.sell_side_levels() << "\n";

    if (auto best_bid = book.best_bid()) {
        std::cout << "Best Bid: " << best_bid->price << " x " << best_bid->total_quantity << "\n";
    }
    if (auto best_ask = book.best_ask()) {
        std::cout << "Best Ask: " << best_ask->price << " x " << best_ask->total_quantity << "\n";
    }
}

template <typename BookFactory, typename AfterRun>
void run_case(
    const char* name,
    const std::vector<latency_lab::MarketMessage>& messages,
    BookFactory make_book,
    AfterRun after_run) {
    using namespace latency_lab;
    using namespace std::chrono;

    auto book = make_book();
    LatencyRecorder latency_recorder;

    const auto benchmark_start = steady_clock::now();

    for (const auto& message : messages) {
        const auto message_start = steady_clock::now();
        book.on_message(message);
        const auto message_end = steady_clock::now();

        const auto latency_ns = duration_cast<nanoseconds>(message_end - message_start).count();
        latency_recorder.record(static_cast<std::uint64_t>(latency_ns));
    }

    const auto benchmark_end = steady_clock::now();
    const auto total_duration_ns = duration_cast<nanoseconds>(benchmark_end - benchmark_start).count();
    const double throughput_msg_per_sec =
        (static_cast<double>(messages.size()) / static_cast<double>(total_duration_ns)) * 1e9;

    std::cout << "\n=== " << name << " ===\n";
    std::cout << "Messages: " << messages.size() << "\n";
    std::cout << "Total Time: " << (static_cast<double>(total_duration_ns) / 1e9) << " seconds\n";
    std::cout << "Throughput: " << (throughput_msg_per_sec / 1e6) << " M msg/sec\n";
    latency_recorder.print_summary();
    after_run(book);
    print_final_book_state(book);
}

}

int main(int argc, char* argv[]) {
    using namespace latency_lab;

    const std::uint64_t num_messages = argc > 1 ? parse_u64_arg(argv[1], 1'000'000) : 1'000'000;
    const std::uint64_t pool_capacity = argc > 2 ? parse_u64_arg(argv[2], num_messages) : num_messages;

    std::cout << "=== Memory Pool Order Book Benchmark ===\n";
    std::cout << "Messages: " << num_messages << "\n";
    std::cout << "Pool Capacity: " << pool_capacity << "\n";

    MarketDataGenerator generator(num_messages, "AAPL");
    generator.generate();
    const auto& messages = generator.messages();

    run_case(
        "OrderBook Baseline",
        messages,
        [] {
            return OrderBook{};
        },
        [](const OrderBook&) {});

    run_case(
        "PooledOrderBook",
        messages,
        [pool_capacity] {
            return PooledOrderBook(static_cast<std::size_t>(pool_capacity));
        },
        [](const PooledOrderBook& book) {
            std::cout << "Pool Allocations: " << book.pool_allocations() << "\n";
            std::cout << "Pool Deallocations: " << book.pool_deallocations() << "\n";
            std::cout << "Pool Available: " << book.pool_available() << "\n";
            std::cout << "Pool Exhaustions: " << book.pool_exhaustions() << "\n";
        });

    return 0;
}
