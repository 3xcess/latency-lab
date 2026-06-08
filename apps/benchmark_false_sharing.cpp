#include "latency_lab/thread_utils.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace {

struct UnpaddedCounters {
    std::atomic<std::uint64_t> first{0};
    std::atomic<std::uint64_t> second{0};
};

struct alignas(64) PaddedCounter {
    std::atomic<std::uint64_t> value{0};
    std::uint8_t padding[64 - sizeof(std::atomic<std::uint64_t>)]{};
};

struct PaddedCounters {
    PaddedCounter first;
    PaddedCounter second;
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

template <typename Counters, typename FirstAccessor, typename SecondAccessor>
void run_case(
    const char* name,
    std::uint64_t iterations,
    int first_cpu,
    int second_cpu,
    FirstAccessor first_accessor,
    SecondAccessor second_accessor) {
    using namespace std::chrono;
    using latency_lab::get_current_cpu;
    using latency_lab::pin_thread_to_cpu;

    Counters counters;
    std::atomic<std::uint64_t> first_migrations{0};
    std::atomic<std::uint64_t> second_migrations{0};

    const auto start = steady_clock::now();

    std::thread first_thread([&] {
        if (first_cpu >= 0) {
            const bool pinned = pin_thread_to_cpu(first_cpu);
            std::cout << name << " first pin result: " << (pinned ? "success" : "unsupported/failed") << "\n";
        }

        int last_cpu = get_current_cpu();
        auto& counter = first_accessor(counters);
        for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
            counter.fetch_add(1, std::memory_order_relaxed);

            if ((iteration & 4095U) != 0U) {
                continue;
            }

            const int current_cpu = get_current_cpu();
            if (last_cpu >= 0 && current_cpu >= 0 && current_cpu != last_cpu) {
                first_migrations.fetch_add(1, std::memory_order_relaxed);
            }
            last_cpu = current_cpu;
        }
    });

    std::thread second_thread([&] {
        if (second_cpu >= 0) {
            const bool pinned = pin_thread_to_cpu(second_cpu);
            std::cout << name << " second pin result: " << (pinned ? "success" : "unsupported/failed") << "\n";
        }

        int last_cpu = get_current_cpu();
        auto& counter = second_accessor(counters);
        for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
            counter.fetch_add(1, std::memory_order_relaxed);

            if ((iteration & 4095U) != 0U) {
                continue;
            }

            const int current_cpu = get_current_cpu();
            if (last_cpu >= 0 && current_cpu >= 0 && current_cpu != last_cpu) {
                second_migrations.fetch_add(1, std::memory_order_relaxed);
            }
            last_cpu = current_cpu;
        }
    });

    first_thread.join();
    second_thread.join();

    const auto end = steady_clock::now();
    const auto elapsed_ns = duration_cast<nanoseconds>(end - start).count();
    const double total_operations = static_cast<double>(iterations) * 2.0;
    const double throughput_ops_per_sec = (total_operations / static_cast<double>(elapsed_ns)) * 1e9;

    std::cout << "\n=== " << name << " ===\n";
    std::cout << "Iterations per thread: " << iterations << "\n";
    std::cout << "Total Time: " << (static_cast<double>(elapsed_ns) / 1e9) << " seconds\n";
    std::cout << "Throughput: " << (throughput_ops_per_sec / 1e6) << " M increments/sec\n";
    std::cout << "First Counter: " << first_accessor(counters).load(std::memory_order_relaxed) << "\n";
    std::cout << "Second Counter: " << second_accessor(counters).load(std::memory_order_relaxed) << "\n";
    std::cout << "First CPU Migrations: " << first_migrations.load(std::memory_order_relaxed) << "\n";
    std::cout << "Second CPU Migrations: " << second_migrations.load(std::memory_order_relaxed) << "\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::uint64_t iterations = argc > 1 ? parse_u64_arg(argv[1], 100'000'000) : 100'000'000;
    const int first_cpu = argc > 2 ? parse_int_arg(argv[2], -1) : -1;
    const int second_cpu = argc > 3 ? parse_int_arg(argv[3], -1) : -1;

    std::cout << "=== False Sharing Benchmark ===\n";
    std::cout << "Iterations per thread: " << iterations << "\n";
    std::cout << "First CPU: " << first_cpu << "\n";
    std::cout << "Second CPU: " << second_cpu << "\n";

    run_case<UnpaddedCounters>(
        "Unpadded Counters",
        iterations,
        first_cpu,
        second_cpu,
        [](UnpaddedCounters& counters) -> std::atomic<std::uint64_t>& {
            return counters.first;
        },
        [](UnpaddedCounters& counters) -> std::atomic<std::uint64_t>& {
            return counters.second;
        });

    run_case<PaddedCounters>(
        "Cache-Line Padded Counters",
        iterations,
        first_cpu,
        second_cpu,
        [](PaddedCounters& counters) -> std::atomic<std::uint64_t>& {
            return counters.first.value;
        },
        [](PaddedCounters& counters) -> std::atomic<std::uint64_t>& {
            return counters.second.value;
        });

    return 0;
}
