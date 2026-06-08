#include "latency_lab/pooled_order_book.hpp"

#include <algorithm>

namespace latency_lab {

PooledOrderBook::PooledOrderBook(std::size_t max_orders)
    : order_pool_(max_orders) {
    order_map_.reserve(max_orders);
}

void PooledOrderBook::on_message(const MarketMessage& msg) {
    switch (msg.type) {
        case MessageType::Add:
            handle_add(msg);
            break;
        case MessageType::Modify:
            handle_modify(msg);
            break;
        case MessageType::Cancel:
            handle_cancel(msg);
            break;
        case MessageType::Trade:
            handle_trade(msg);
            break;
    }
}

void PooledOrderBook::handle_add(const MarketMessage& msg) {
    if (order_map_.find(msg.order_id) != order_map_.end()) {
        return;
    }

    Order* order = order_pool_.allocate(Order{
        .order_id = msg.order_id,
        .price = msg.price,
        .quantity = msg.quantity,
        .side = msg.side,
    });

    if (order == nullptr) {
        ++pool_exhaustions_;
        return;
    }

    order_map_.emplace(msg.order_id, order);

    auto add_to_level = [&](auto& side_map) {
        auto level_it = side_map.find(msg.price);
        if (level_it != side_map.end()) {
            level_it->second.order_ids.push_back(msg.order_id);
            level_it->second.total_quantity += msg.quantity;
            return;
        }

        PriceLevel level;
        level.price = msg.price;
        level.order_ids.push_back(msg.order_id);
        level.total_quantity = msg.quantity;
        side_map.emplace(msg.price, level);
    };

    if (msg.side == Side::Buy) {
        add_to_level(bids_);
    } else {
        add_to_level(asks_);
    }
}

void PooledOrderBook::handle_modify(const MarketMessage& msg) {
    auto order_it = order_map_.find(msg.order_id);
    if (order_it == order_map_.end()) {
        return;
    }

    Order& order = *order_it->second;
    auto modify_level = [&](auto& side_map) {
        auto level_it = side_map.find(order.price);
        if (level_it == side_map.end()) {
            return;
        }

        const auto qty_delta = static_cast<std::int64_t>(msg.quantity) - static_cast<std::int64_t>(order.quantity);
        const auto updated_quantity = static_cast<std::int64_t>(level_it->second.total_quantity) + qty_delta;
        level_it->second.total_quantity = updated_quantity > 0 ? static_cast<std::uint32_t>(updated_quantity) : 0;
        order.quantity = msg.quantity;
    };

    if (order.side == Side::Buy) {
        modify_level(bids_);
    } else {
        modify_level(asks_);
    }
}

void PooledOrderBook::handle_cancel(const MarketMessage& msg) {
    auto order_it = order_map_.find(msg.order_id);
    if (order_it == order_map_.end()) {
        return;
    }

    Order* order = order_it->second;
    auto cancel_level = [&](auto& side_map) {
        auto level_it = side_map.find(order->price);
        if (level_it != side_map.end()) {
            auto& order_ids = level_it->second.order_ids;
            auto id_it = std::find(order_ids.begin(), order_ids.end(), msg.order_id);
            if (id_it != order_ids.end()) {
                order_ids.erase(id_it);
            }
            level_it->second.total_quantity -= order->quantity;

            if (order_ids.empty()) {
                side_map.erase(level_it);
            }
        }
    };

    if (order->side == Side::Buy) {
        cancel_level(bids_);
    } else {
        cancel_level(asks_);
    }

    order_map_.erase(order_it);
    order_pool_.deallocate(order);
}

void PooledOrderBook::handle_trade(const MarketMessage& msg) {
    auto order_it = order_map_.find(msg.order_id);
    if (order_it == order_map_.end()) {
        return;
    }

    Order* order = order_it->second;
    auto trade_level = [&](auto& side_map) {
        auto level_it = side_map.find(order->price);
        if (level_it == side_map.end()) {
            return;
        }

        const std::uint32_t trade_qty = std::min(msg.quantity, order->quantity);
        level_it->second.total_quantity -= trade_qty;
        order->quantity -= trade_qty;

        if (order->quantity != 0) {
            return;
        }

        auto& order_ids = level_it->second.order_ids;
        auto id_it = std::find(order_ids.begin(), order_ids.end(), msg.order_id);
        if (id_it != order_ids.end()) {
            order_ids.erase(id_it);
        }

        if (order_ids.empty()) {
            side_map.erase(level_it);
        }

        order_map_.erase(order_it);
        order_pool_.deallocate(order);
    };

    if (order->side == Side::Buy) {
        trade_level(bids_);
    } else {
        trade_level(asks_);
    }
}

std::optional<PriceLevel> PooledOrderBook::best_bid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->second;
}

std::optional<PriceLevel> PooledOrderBook::best_ask() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->second;
}

}
