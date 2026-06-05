#pragma once

#include "market_message.hpp"
#include <map>
#include <unordered_map>
#include <optional>
#include <vector>
#include <cstdint>
#include <memory>

namespace latency_lab {

struct Order {
    std::uint64_t order_id;
    std::int64_t price;
    std::uint32_t quantity;
    Side side;
};

struct PriceLevel {
    std::int64_t price;
    std::vector<std::uint64_t> order_ids;
    std::uint32_t total_quantity = 0;

    std::uint32_t aggregate_quantity() const;
};

class OrderBook {
public:
    OrderBook() = default;
    ~OrderBook() = default;
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    void on_message(const MarketMessage& msg);
    std::optional<PriceLevel> best_bid() const;
    std::optional<PriceLevel> best_ask() const;

    std::uint64_t total_orders() const { return order_map_.size(); }
    std::uint32_t buy_side_levels() const { return bids_.size(); }
    std::uint32_t sell_side_levels() const { return asks_.size(); }

private:
    std::map<std::int64_t, PriceLevel, std::greater<std::int64_t>> bids_;
    std::map<std::int64_t, PriceLevel, std::less<std::int64_t>> asks_;
    std::unordered_map<std::uint64_t, Order> order_map_;

    void handle_add(const MarketMessage& msg);
    void handle_modify(const MarketMessage& msg);
    void handle_cancel(const MarketMessage& msg);
    void handle_trade(const MarketMessage& msg);

    std::map<std::int64_t, PriceLevel, std::greater<std::int64_t>>& get_bid_asks() {
        return bids_;
    }
    std::map<std::int64_t, PriceLevel, std::less<std::int64_t>>& get_asks() {
        return asks_;
    }
};

}
