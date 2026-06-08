#include "latency_lab/market_message.hpp"
#include "latency_lab/generator.hpp"
#include "latency_lab/order_book.hpp"
#include <iostream>
#include <cassert>

// Simple assert-based tests for order book correctness.
// Decision: Use simple assert() for MVP testing.
// This avoids external test framework dependencies and is sufficient for Phase 1.

namespace {
using namespace latency_lab;

void test_add_buy_order() {
    std::cout << "Test: add_buy_order... ";
    OrderBook book;

    MarketMessage msg{};
    msg.order_id = 1;
    msg.price = 10000;
    msg.quantity = 100;
    msg.side = Side::Buy;
    msg.type = MessageType::Add;

    book.on_message(msg);

    auto best_bid = book.best_bid();
    assert(best_bid.has_value());
    assert(best_bid->price == 10000);
    assert(best_bid->total_quantity == 100);
    assert(book.total_orders() == 1);
    std::cout << "PASS\n";
}

void test_add_sell_order() {
    std::cout << "Test: add_sell_order... ";
    OrderBook book;

    MarketMessage msg{};
    msg.order_id = 1;
    msg.price = 10500;
    msg.quantity = 50;
    msg.side = Side::Sell;
    msg.type = MessageType::Add;

    book.on_message(msg);

    auto best_ask = book.best_ask();
    assert(best_ask.has_value());
    assert(best_ask->price == 10500);
    assert(best_ask->total_quantity == 50);
    assert(book.total_orders() == 1);
    std::cout << "PASS\n";
}

void test_best_bid_updates() {
    std::cout << "Test: best_bid_updates... ";
    OrderBook book;

    MarketMessage msg1{};
    msg1.order_id = 1;
    msg1.price = 10000;
    msg1.quantity = 100;
    msg1.side = Side::Buy;
    msg1.type = MessageType::Add;
    book.on_message(msg1);

    MarketMessage msg2{};
    msg2.order_id = 2;
    msg2.price = 10100;
    msg2.quantity = 50;
    msg2.side = Side::Buy;
    msg2.type = MessageType::Add;
    book.on_message(msg2);

    auto best_bid = book.best_bid();
    assert(best_bid->price == 10100);
    std::cout << "PASS\n";
}

void test_cancel_order() {
    std::cout << "Test: cancel_order... ";
    OrderBook book;

    MarketMessage add_msg{};
    add_msg.order_id = 1;
    add_msg.price = 10000;
    add_msg.quantity = 100;
    add_msg.side = Side::Buy;
    add_msg.type = MessageType::Add;
    book.on_message(add_msg);
    assert(book.total_orders() == 1);

    MarketMessage cancel_msg{};
    cancel_msg.order_id = 1;
    cancel_msg.type = MessageType::Cancel;
    book.on_message(cancel_msg);

    assert(book.total_orders() == 0);
    assert(!book.best_bid().has_value());
    std::cout << "PASS\n";
}

void test_modify_order() {
    std::cout << "Test: modify_order... ";
    OrderBook book;

    MarketMessage add_msg{};
    add_msg.order_id = 1;
    add_msg.price = 10000;
    add_msg.quantity = 100;
    add_msg.side = Side::Buy;
    add_msg.type = MessageType::Add;
    book.on_message(add_msg);

    auto best_bid_before = book.best_bid();
    assert(best_bid_before->total_quantity == 100);

    MarketMessage modify_msg{};
    modify_msg.order_id = 1;
    modify_msg.quantity = 150;
    modify_msg.type = MessageType::Modify;
    book.on_message(modify_msg);

    auto best_bid_after = book.best_bid();
    assert(best_bid_after->total_quantity == 150);
    assert(book.total_orders() == 1);
    std::cout << "PASS\n";
}

void test_trade_partial_fill() {
    std::cout << "Test: trade_partial_fill... ";
    OrderBook book;

    MarketMessage add_msg{};
    add_msg.order_id = 1;
    add_msg.price = 10000;
    add_msg.quantity = 100;
    add_msg.side = Side::Buy;
    add_msg.type = MessageType::Add;
    book.on_message(add_msg);

    MarketMessage trade_msg{};
    trade_msg.order_id = 1;
    trade_msg.quantity = 30;
    trade_msg.type = MessageType::Trade;
    book.on_message(trade_msg);

    auto best_bid = book.best_bid();
    assert(best_bid->total_quantity == 70);
    assert(book.total_orders() == 1);
    std::cout << "PASS\n";
}

void test_trade_full_fill() {
    std::cout << "Test: trade_full_fill... ";
    OrderBook book;

    MarketMessage add_msg{};
    add_msg.order_id = 1;
    add_msg.price = 10000;
    add_msg.quantity = 100;
    add_msg.side = Side::Buy;
    add_msg.type = MessageType::Add;
    book.on_message(add_msg);

    MarketMessage trade_msg{};
    trade_msg.order_id = 1;
    trade_msg.quantity = 100;
    trade_msg.type = MessageType::Trade;
    book.on_message(trade_msg);

    assert(book.total_orders() == 0);
    assert(!book.best_bid().has_value());
    std::cout << "PASS\n";
}

void test_multiple_orders_same_price() {
    std::cout << "Test: multiple_orders_same_price... ";
    OrderBook book;

    for (int i = 0; i < 2; ++i) {
        MarketMessage msg{};
        msg.order_id = 100 + i;
        msg.price = 10000;
        msg.quantity = 100;
        msg.side = Side::Buy;
        msg.type = MessageType::Add;
        book.on_message(msg);
    }

    auto best_bid = book.best_bid();
    assert(best_bid->total_quantity == 200);
    assert(book.total_orders() == 2);
    std::cout << "PASS\n";
}

void test_duplicate_add_is_ignored() {
    std::cout << "Test: duplicate_add_is_ignored... ";
    OrderBook book;

    MarketMessage first{};
    first.order_id = 1;
    first.price = 10000;
    first.quantity = 100;
    first.side = Side::Buy;
    first.type = MessageType::Add;
    book.on_message(first);

    MarketMessage duplicate{};
    duplicate.order_id = 1;
    duplicate.price = 10100;
    duplicate.quantity = 50;
    duplicate.side = Side::Buy;
    duplicate.type = MessageType::Add;
    book.on_message(duplicate);

    auto best_bid = book.best_bid();
    assert(best_bid.has_value());
    assert(best_bid->price == 10000);
    assert(best_bid->total_quantity == 100);
    assert(book.total_orders() == 1);
    assert(book.buy_side_levels() == 1);
    std::cout << "PASS\n";
}

}

int main() {
    std::cout << "=== Order Book Unit Tests ===\n\n";

    try {
        test_add_buy_order();
        test_add_sell_order();
        test_best_bid_updates();
        test_cancel_order();
        test_modify_order();
        test_trade_partial_fill();
        test_trade_full_fill();
        test_multiple_orders_same_price();
        test_duplicate_add_is_ignored();

        std::cout << "\nAll tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
