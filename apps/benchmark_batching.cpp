#include "latency_lab/generator.hpp"
#include "latency_lab/metrics.hpp"
#include "latency_lab/order_book.hpp"

#include <algorithm>
#include <chrono>
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

std::vector<std::size_t> default_batch_sizes() {
    return {1, 8, 32, 64};
}

}

int main(int argc, char* argv[]) {
    using namespace latency_lab;
    using namespace std::chrono;

    const std::uint64_t num_messages = argc > 1 ? parse_u64_arg(argv[1], 1'000'000) : 1'000'000;

    std::cout << "=== Batching Order Book Benchmark ===\n";
    std::cout << "Messages: " << num_messages << "\n";
    std::cout << "Latency metric: amortized batch processing time per message\n";

    MarketDataGenerator generator(num_messages, "AAPL");
    generator.generate();
    const auto& messages = generator.messages();

    for (const std::size_t batch_size : default_batch_sizes()) {
        OrderBook book;
        LatencyRecorder amortized_latency_recorder;

        const auto benchmark_start = steady_clock::now();

        for (std::size_t batch_start = 0; batch_start < messages.size(); batch_start += batch_size) {
            const std::size_t batch_end = std::min(batch_start + batch_size, messages.size());

            const auto batch_timer_start = steady_clock::now();
            for (std::size_t message_index = batch_start; message_index < batch_end; ++message_index) {
                book.on_message(messages[message_index]);
            }
            const auto batch_timer_end = steady_clock::now();

            const auto batch_duration_ns = duration_cast<nanoseconds>(batch_timer_end - batch_timer_start).count();
            const auto processed_in_batch = static_cast<std::uint64_t>(batch_end - batch_start);
            const auto amortized_latency_ns =
                static_cast<std::uint64_t>(batch_duration_ns) / std::max<std::uint64_t>(processed_in_batch, 1);

            for (std::uint64_t sample_index = 0; sample_index < processed_in_batch; ++sample_index) {
                amortized_latency_recorder.record(amortized_latency_ns);
            }
        }

        const auto benchmark_end = steady_clock::now();
        const auto total_duration_ns = duration_cast<nanoseconds>(benchmark_end - benchmark_start).count();
        const double throughput_msg_per_sec =
            (static_cast<double>(num_messages) / static_cast<double>(total_duration_ns)) * 1e9;

        std::cout << "\n=== Batch Size " << batch_size << " ===\n";
        std::cout << "Total Time: " << (static_cast<double>(total_duration_ns) / 1e9) << " seconds\n";
        std::cout << "Throughput: " << (throughput_msg_per_sec / 1e6) << " M msg/sec\n";
        amortized_latency_recorder.print_summary();
        std::cout << "Total Orders in Book: " << book.total_orders() << "\n";
        std::cout << "Buy Side Levels: " << book.buy_side_levels() << "\n";
        std::cout << "Sell Side Levels: " << book.sell_side_levels() << "\n";
    }

    return 0;
}
