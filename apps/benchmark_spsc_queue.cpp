#include "latency_lab/generator.hpp"
#include "latency_lab/market_message.hpp"
#include "latency_lab/metrics.hpp"
#include "latency_lab/order_book.hpp"
#include "latency_lab/spsc_ring_buffer.hpp"
#include "latency_lab/thread_utils.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace {

struct MarketMessageEnvelope {
    latency_lab::MarketMessage message;
    std::uint64_t producer_timestamp_ns = 0;
    std::uint64_t queue_push_timestamp_ns = 0;
};

std::uint64_t parse_u64_arg(char* value, std::uint64_t fallback) {
    if (value == nullptr) {
        return fallback;
    }
    return std::stoull(value);
}

int parse_int_arg(char* value, int fallback) {
    if (value == nullptr) {
        return fallback;
    }
    return std::stoi(value);
}

}  // namespace

int main(int argc, char* argv[]) {
    using namespace latency_lab;
    using namespace std::chrono;

    const std::uint64_t num_messages = argc > 1 ? parse_u64_arg(argv[1], 1'000'000) : 1'000'000;
    const std::uint64_t queue_capacity_arg = argc > 2 ? parse_u64_arg(argv[2], 65'536) : 65'536;
    const int producer_cpu = argc > 3 ? parse_int_arg(argv[3], -1) : -1;
    const int consumer_cpu = argc > 4 ? parse_int_arg(argv[4], -1) : -1;
    const auto queue_capacity = static_cast<std::size_t>(queue_capacity_arg);

    std::cout << "=== SPSC Ring Buffer Order Book Benchmark ===\n";
    std::cout << "Messages: " << num_messages << "\n";
    std::cout << "Queue Capacity: " << queue_capacity << "\n";
    std::cout << "Producer CPU: " << producer_cpu << "\n";
    std::cout << "Consumer CPU: " << consumer_cpu << "\n";

    MarketDataGenerator generator(num_messages, "AAPL");
    generator.generate();

    SpscRingBuffer<MarketMessageEnvelope> queue(queue_capacity);
    OrderBook book;
    LatencyRecorder pipeline_latency_recorder;
    LatencyRecorder queue_wait_recorder;
    LatencyRecorder book_processing_recorder;

    std::atomic<bool> producer_done{false};
    std::atomic<std::uint64_t> produced{0};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<std::uint64_t> producer_spins{0};
    std::atomic<std::uint64_t> consumer_spins{0};
    std::atomic<std::uint64_t> producer_migrations{0};
    std::atomic<std::uint64_t> consumer_migrations{0};

    const auto benchmark_start = steady_clock::now();

    std::thread producer([&] {
        if (producer_cpu >= 0) {
            const bool pinned = pin_thread_to_cpu(producer_cpu);
            std::cout << "Producer pin result: " << (pinned ? "success" : "unsupported/failed") << "\n";
        }

        int last_cpu = get_current_cpu();
        for (const auto& message : generator.messages()) {
            MarketMessageEnvelope envelope;
            envelope.message = message;
            envelope.producer_timestamp_ns = get_timestamp_ns();

            while (true) {
                envelope.queue_push_timestamp_ns = get_timestamp_ns();
                if (queue.push(envelope)) {
                    break;
                }
                producer_spins.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }

            produced.fetch_add(1, std::memory_order_relaxed);

            const int current_cpu = get_current_cpu();
            if (last_cpu >= 0 && current_cpu >= 0 && current_cpu != last_cpu) {
                producer_migrations.fetch_add(1, std::memory_order_relaxed);
            }
            last_cpu = current_cpu;
        }

        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        if (consumer_cpu >= 0) {
            const bool pinned = pin_thread_to_cpu(consumer_cpu);
            std::cout << "Consumer pin result: " << (pinned ? "success" : "unsupported/failed") << "\n";
        }

        int last_cpu = get_current_cpu();
        MarketMessageEnvelope envelope{};
        while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
            if (!queue.pop(envelope)) {
                consumer_spins.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
                continue;
            }

            const auto pop_timestamp_ns = get_timestamp_ns();
            queue_wait_recorder.record(pop_timestamp_ns - envelope.queue_push_timestamp_ns);

            const auto book_start = steady_clock::now();
            book.on_message(envelope.message);
            const auto book_end = steady_clock::now();

            const auto book_latency_ns = duration_cast<nanoseconds>(book_end - book_start).count();
            book_processing_recorder.record(static_cast<std::uint64_t>(book_latency_ns));

            const auto end_timestamp_ns = get_timestamp_ns();
            pipeline_latency_recorder.record(end_timestamp_ns - envelope.producer_timestamp_ns);
            consumed.fetch_add(1, std::memory_order_relaxed);

            const int current_cpu = get_current_cpu();
            if (last_cpu >= 0 && current_cpu >= 0 && current_cpu != last_cpu) {
                consumer_migrations.fetch_add(1, std::memory_order_relaxed);
            }
            last_cpu = current_cpu;
        }
    });

    producer.join();
    consumer.join();

    const auto benchmark_end = steady_clock::now();
    const auto total_duration_ns = duration_cast<nanoseconds>(benchmark_end - benchmark_start).count();
    const double throughput_msg_per_sec =
        (static_cast<double>(consumed.load(std::memory_order_relaxed)) / static_cast<double>(total_duration_ns)) * 1e9;

    std::cout << "\n=== Benchmark Results ===\n";
    std::cout << "Produced: " << produced.load(std::memory_order_relaxed) << "\n";
    std::cout << "Consumed: " << consumed.load(std::memory_order_relaxed) << "\n";
    std::cout << "Total Time: " << (static_cast<double>(total_duration_ns) / 1e9) << " seconds\n";
    std::cout << "Throughput: " << (throughput_msg_per_sec / 1e6) << " M msg/sec\n";
    std::cout << "Producer Spins: " << producer_spins.load(std::memory_order_relaxed) << "\n";
    std::cout << "Consumer Spins: " << consumer_spins.load(std::memory_order_relaxed) << "\n";
    std::cout << "Producer CPU Migrations: " << producer_migrations.load(std::memory_order_relaxed) << "\n";
    std::cout << "Consumer CPU Migrations: " << consumer_migrations.load(std::memory_order_relaxed) << "\n";

    std::cout << "\n=== Pipeline Latency ===\n";
    std::cout << "producer timestamp -> consumer finished processing\n";
    pipeline_latency_recorder.print_summary();

    std::cout << "\n=== Queue Wait / Residence Time ===\n";
    std::cout << "producer push timestamp -> consumer pop timestamp\n";
    queue_wait_recorder.print_summary();

    std::cout << "\n=== Book Processing Latency ===\n";
    std::cout << "before OrderBook::on_message() -> after OrderBook::on_message()\n";
    book_processing_recorder.print_summary();

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
