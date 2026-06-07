#include "latency_lab/generator.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace latency_lab {

MarketDataGenerator::MarketDataGenerator(std::uint64_t num_messages, const char* symbol)
    : num_messages_(num_messages), seed_(12345ULL), next_order_id_(1000), sequence_number_(1) {
    std::memset(symbol_, ' ', sizeof(symbol_));
    const auto symbol_length = std::min<std::size_t>(sizeof(symbol_), std::strlen(symbol));
    std::memcpy(symbol_, symbol, symbol_length);
}

void MarketDataGenerator::generate() {
    messages_.reserve(num_messages_);
    for (std::uint64_t i = 0; i < num_messages_; ++i) {
        MarketMessage msg = {};
        msg.sequence_number = sequence_number_++;
        msg.exchange_timestamp_ns = get_timestamp_ns();
        msg.order_id = next_order_id_ + (i % 10000);
        std::memcpy(msg.symbol, symbol_, 8);

        msg.price = 10000 + static_cast<std::int64_t>(random_bounded(19000));
        msg.quantity = 100 + static_cast<std::uint32_t>(random_bounded(10000));
        msg.side = random_bounded(2) == 0 ? Side::Buy : Side::Sell;

        std::uint32_t type_rand = static_cast<std::uint32_t>(random_bounded(100));
        if (type_rand < 70) {
            msg.type = MessageType::Add;
        } else if (type_rand < 85) {
            msg.type = MessageType::Modify;
        } else if (type_rand < 95) {
            msg.type = MessageType::Cancel;
        } else {
            msg.type = MessageType::Trade;
        }

        messages_.push_back(msg);
    }
}

}
