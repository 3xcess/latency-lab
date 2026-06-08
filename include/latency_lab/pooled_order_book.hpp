#pragma once

#include "latency_lab/market_message.hpp"
#include "latency_lab/object_pool.hpp"
#include "latency_lab/order_book.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>

namespace latency_lab {

class PooledOrderBook {
public:
    explicit PooledOrderBook(std::size_t max_orders);

    PooledOrderBook(const PooledOrderBook&) = delete;
    PooledOrderBook& operator=(const PooledOrderBook&) = delete;

    void on_message(const MarketMessage& msg);
    std::optional<PriceLevel> best_bid() const;
    std::optional<PriceLevel> best_ask() const;

    std::uint64_t total_orders() const noexcept {
        return order_map_.size();
    }

    std::uint32_t buy_side_levels() const {
        return static_cast<std::uint32_t>(bids_.size());
    }

    std::uint32_t sell_side_levels() const {
        return static_cast<std::uint32_t>(asks_.size());
    }

    std::uint64_t pool_allocations() const noexcept {
        return order_pool_.allocations();
    }

    std::uint64_t pool_deallocations() const noexcept {
        return order_pool_.deallocations();
    }

    std::uint64_t pool_exhaustions() const noexcept {
        return pool_exhaustions_;
    }

    std::size_t pool_available() const noexcept {
        return order_pool_.available();
    }

private:
    std::map<std::int64_t, PriceLevel, std::greater<std::int64_t>> bids_;
    std::map<std::int64_t, PriceLevel, std::less<std::int64_t>> asks_;
    std::unordered_map<std::uint64_t, Order*> order_map_;
    ObjectPool<Order> order_pool_;
    std::uint64_t pool_exhaustions_ = 0;

    void handle_add(const MarketMessage& msg);
    void handle_modify(const MarketMessage& msg);
    void handle_cancel(const MarketMessage& msg);
    void handle_trade(const MarketMessage& msg);
};

}
