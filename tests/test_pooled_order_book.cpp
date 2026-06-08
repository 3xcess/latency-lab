#include "latency_lab/market_message.hpp"
#include "latency_lab/pooled_order_book.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace {

latency_lab::MarketMessage make_message(
    latency_lab::MessageType type,
    std::uint64_t order_id,
    latency_lab::Side side,
    std::int64_t price,
    std::uint32_t quantity) {
    latency_lab::MarketMessage message{};
    message.type = type;
    message.order_id = order_id;
    message.side = side;
    message.price = price;
    message.quantity = quantity;
    return message;
}

void test_add_and_best_levels() {
    latency_lab::PooledOrderBook book(16);
    book.on_message(make_message(latency_lab::MessageType::Add, 1, latency_lab::Side::Buy, 100, 10));
    book.on_message(make_message(latency_lab::MessageType::Add, 2, latency_lab::Side::Sell, 105, 20));

    const auto best_bid = book.best_bid();
    const auto best_ask = book.best_ask();

    assert(best_bid.has_value());
    assert(best_bid->price == 100);
    assert(best_bid->total_quantity == 10);
    assert(best_ask.has_value());
    assert(best_ask->price == 105);
    assert(best_ask->total_quantity == 20);
    assert(book.pool_allocations() == 2);
}

void test_cancel_recycles_order() {
    latency_lab::PooledOrderBook book(1);
    book.on_message(make_message(latency_lab::MessageType::Add, 1, latency_lab::Side::Buy, 100, 10));
    book.on_message(make_message(latency_lab::MessageType::Cancel, 1, latency_lab::Side::Buy, 100, 0));
    book.on_message(make_message(latency_lab::MessageType::Add, 2, latency_lab::Side::Sell, 101, 5));

    assert(book.total_orders() == 1);
    assert(book.pool_allocations() == 2);
    assert(book.pool_deallocations() == 1);
    assert(book.pool_exhaustions() == 0);
    assert(book.best_ask().has_value());
}

void test_pool_exhaustion_drops_add() {
    latency_lab::PooledOrderBook book(1);
    book.on_message(make_message(latency_lab::MessageType::Add, 1, latency_lab::Side::Buy, 100, 10));
    book.on_message(make_message(latency_lab::MessageType::Add, 2, latency_lab::Side::Sell, 101, 5));

    assert(book.total_orders() == 1);
    assert(book.pool_allocations() == 1);
    assert(book.pool_exhaustions() == 1);
}

void test_trade_removes_filled_order() {
    latency_lab::PooledOrderBook book(4);
    book.on_message(make_message(latency_lab::MessageType::Add, 1, latency_lab::Side::Buy, 100, 10));
    book.on_message(make_message(latency_lab::MessageType::Trade, 1, latency_lab::Side::Buy, 100, 10));

    assert(book.total_orders() == 0);
    assert(!book.best_bid().has_value());
    assert(book.pool_deallocations() == 1);
}

void test_duplicate_add_is_ignored() {
    latency_lab::PooledOrderBook book(4);
    book.on_message(make_message(latency_lab::MessageType::Add, 1, latency_lab::Side::Buy, 100, 10));
    book.on_message(make_message(latency_lab::MessageType::Add, 1, latency_lab::Side::Buy, 101, 20));

    const auto best_bid = book.best_bid();
    assert(best_bid.has_value());
    assert(best_bid->price == 100);
    assert(best_bid->total_quantity == 10);
    assert(book.total_orders() == 1);
    assert(book.buy_side_levels() == 1);
    assert(book.pool_allocations() == 1);
}

}

int main() {
    std::cout << "=== Pooled Order Book Unit Tests ===\n";

    test_add_and_best_levels();
    test_cancel_recycles_order();
    test_pool_exhaustion_drops_add();
    test_trade_removes_filled_order();
    test_duplicate_add_is_ignored();

    std::cout << "All pooled order book tests passed!\n";
    return 0;
}
