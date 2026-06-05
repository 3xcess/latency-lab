#include "latency_lab/generator.hpp"
#include <cstring>
#include <algorithm>

namespace latency_lab {

MarketDataGenerator::MarketDataGenerator(std::uint64_t num_messages, const char* symbol)
    : num_messages_(num_messages), seed_(12345ULL), next_order_id_(1000), sequence_number_(1) {
    std::memset(symbol_, ' ', sizeof(symbol_));
    std::strncpy(symbol_, symbol, std::min(size_t(7), std::strlen(symbol)));
}

void MarketDataGenerator::generate() {
    messages_.reserve(num_messages_);
    for (std::uint64_t i = 0; i < num_messages_; ++i) {
        MarketMessage msg = {};
        msg.sequence_number = sequence_number_++;
        msg.exchange_timestamp_ns = get_timestamp_ns();
        msg.order_id = next_order_id_ + (i % 10000);
        std::memcpy(msg.symbol, symbol_, 8);

        msg.price = 10000 + (next_random() % 19000);
        msg.quantity = 100 + (next_random() % 10000);
        msg.side = (next_random() % 2) == 0 ? Side::Buy : Side::Sell;

        std::uint32_t type_rand = next_random() % 100;
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
