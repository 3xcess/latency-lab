#include "latency_lab/generator.hpp"
#include "latency_lab/market_message.hpp"
#include "latency_lab/metrics.hpp"
#include "latency_lab/order_book.hpp"
#include "latency_lab/socket_utils.hpp"
#include "latency_lab/thread_utils.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

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

bool parse_bool_arg(char* value, bool fallback) {
    if (value == nullptr) {
        return fallback;
    }
    return std::stoi(value) != 0;
}

int create_tcp_listener(int receive_buffer_bytes) {
    const int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        throw std::runtime_error("socket(AF_INET, SOCK_STREAM) failed");
    }

    const int reuse_addr = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));
    latency_lab::set_socket_receive_buffer(listen_fd, receive_buffer_bytes);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(latency_lab::loopback_port_auto);

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        latency_lab::close_socket(listen_fd);
        throw std::runtime_error("bind() failed");
    }

    if (listen(listen_fd, 1) != 0) {
        latency_lab::close_socket(listen_fd);
        throw std::runtime_error("listen() failed");
    }

    return listen_fd;
}

int connect_tcp_client(int port, bool tcp_no_delay, int send_buffer_bytes) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("client socket() failed");
    }

    latency_lab::set_tcp_no_delay(fd, tcp_no_delay);
    latency_lab::set_socket_send_buffer(fd, send_buffer_bytes);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<std::uint16_t>(port));

    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        latency_lab::close_socket(fd);
        throw std::runtime_error("connect() failed");
    }

    return fd;
}

}

int main(int argc, char* argv[]) {
    using namespace latency_lab;
    using namespace std::chrono;

    const std::uint64_t num_messages = argc > 1 ? parse_u64_arg(argv[1], 1'000'000) : 1'000'000;
    const bool tcp_no_delay = argc > 2 ? parse_bool_arg(argv[2], true) : true;
    const int send_buffer_bytes = argc > 3 ? parse_int_arg(argv[3], 0) : 0;
    const int receive_buffer_bytes = argc > 4 ? parse_int_arg(argv[4], 0) : 0;
    const int producer_cpu = argc > 5 ? parse_int_arg(argv[5], -1) : -1;
    const int consumer_cpu = argc > 6 ? parse_int_arg(argv[6], -1) : -1;

    std::cout << "=== TCP Feed Order Book Benchmark ===\n";
    std::cout << "Messages: " << num_messages << "\n";
    std::cout << "TCP_NODELAY: " << (tcp_no_delay ? "on" : "off") << "\n";
    std::cout << "SO_SNDBUF: " << send_buffer_bytes << "\n";
    std::cout << "SO_RCVBUF: " << receive_buffer_bytes << "\n";
    std::cout << "Producer CPU: " << producer_cpu << "\n";
    std::cout << "Consumer CPU: " << consumer_cpu << "\n";

    MarketDataGenerator generator(num_messages, "AAPL");
    generator.generate();

    const int listen_fd = create_tcp_listener(receive_buffer_bytes);
    const int port = get_bound_port(listen_fd);
    if (port <= 0) {
        close_socket(listen_fd);
        throw std::runtime_error("failed to discover listener port");
    }

    OrderBook book;
    LatencyRecorder network_to_book_latency;
    LatencyRecorder book_processing_latency;
    std::atomic<std::uint64_t> produced{0};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<std::uint64_t> producer_migrations{0};
    std::atomic<std::uint64_t> consumer_migrations{0};
    std::atomic<bool> accept_ready{false};

    const auto benchmark_start = steady_clock::now();

    std::thread consumer([&] {
        if (consumer_cpu >= 0) {
            const bool pinned = pin_thread_to_cpu(consumer_cpu);
            std::cout << "Consumer pin result: " << (pinned ? "success" : "unsupported/failed") << "\n";
        }

        accept_ready.store(true, std::memory_order_release);
        const int accepted_fd = accept(listen_fd, nullptr, nullptr);
        close_socket(listen_fd);
        if (accepted_fd < 0) {
            return;
        }

        set_tcp_no_delay(accepted_fd, tcp_no_delay);
        set_socket_receive_buffer(accepted_fd, receive_buffer_bytes);

        int last_cpu = get_current_cpu();
        MarketMessage message{};
        while (consumed.load(std::memory_order_relaxed) < num_messages) {
            if (!recv_all(accepted_fd, &message, sizeof(message))) {
                break;
            }

            const auto before_book_ns = get_timestamp_ns();
            network_to_book_latency.record(before_book_ns - message.exchange_timestamp_ns);

            const auto book_start = steady_clock::now();
            book.on_message(message);
            const auto book_end = steady_clock::now();
            book_processing_latency.record(static_cast<std::uint64_t>(duration_cast<nanoseconds>(book_end - book_start).count()));

            consumed.fetch_add(1, std::memory_order_relaxed);

            const int current_cpu = get_current_cpu();
            if (last_cpu >= 0 && current_cpu >= 0 && current_cpu != last_cpu) {
                consumer_migrations.fetch_add(1, std::memory_order_relaxed);
            }
            last_cpu = current_cpu;
        }

        close_socket(accepted_fd);
    });

    std::thread producer([&] {
        if (producer_cpu >= 0) {
            const bool pinned = pin_thread_to_cpu(producer_cpu);
            std::cout << "Producer pin result: " << (pinned ? "success" : "unsupported/failed") << "\n";
        }

        while (!accept_ready.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        const int fd = connect_tcp_client(port, tcp_no_delay, send_buffer_bytes);
        int last_cpu = get_current_cpu();

        for (auto message : generator.messages()) {
            message.exchange_timestamp_ns = get_timestamp_ns();
            if (!send_all(fd, &message, sizeof(message))) {
                break;
            }

            produced.fetch_add(1, std::memory_order_relaxed);

            const int current_cpu = get_current_cpu();
            if (last_cpu >= 0 && current_cpu >= 0 && current_cpu != last_cpu) {
                producer_migrations.fetch_add(1, std::memory_order_relaxed);
            }
            last_cpu = current_cpu;
        }

        shutdown(fd, SHUT_WR);
        close_socket(fd);
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
    std::cout << "Producer CPU Migrations: " << producer_migrations.load(std::memory_order_relaxed) << "\n";
    std::cout << "Consumer CPU Migrations: " << consumer_migrations.load(std::memory_order_relaxed) << "\n";

    std::cout << "\n=== Network To Book Latency ===\n";
    std::cout << "producer send timestamp -> before OrderBook::on_message()\n";
    network_to_book_latency.print_summary();

    std::cout << "\n=== Book Processing Latency ===\n";
    std::cout << "before OrderBook::on_message() -> after OrderBook::on_message()\n";
    book_processing_latency.print_summary();

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
