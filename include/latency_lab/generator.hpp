#pragma once

#include "market_message.hpp"
#include <vector>
#include <cstdint>

namespace latency_lab {


class MarketDataGenerator {
public:
    explicit MarketDataGenerator(std::uint64_t num_messages, const char* symbol = "AAPL");

    void generate();
    const std::vector<MarketMessage>& messages() const { return messages_; }

private:
    std::uint64_t next_random() noexcept {
        constexpr std::uint64_t a = 1664525ULL;
        constexpr std::uint64_t c = 1013904223ULL;
        seed_ = a * seed_ + c;
        return seed_;
    }

    std::uint64_t num_messages_;
    std::uint64_t seed_ = 12345ULL; // Fixed for reproducibility
    char symbol_[8];
    std::vector<MarketMessage> messages_;
    std::uint64_t next_order_id_ = 1000;
    std::uint64_t sequence_number_ = 1;
};

}
