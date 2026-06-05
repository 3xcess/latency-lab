#pragma once

#include <cstdint>
#include <chrono>

namespace latency_lab {

enum class Side : std::uint8_t {
    Buy = 0,
    Sell = 1,
};

enum class MessageType : std::uint8_t {
    Add = 0,
    Modify = 1,
    Cancel = 2,
    Trade = 3,
};

struct alignas(64) MarketMessage {
    std::uint64_t sequence_number;      
    std::uint64_t exchange_timestamp_ns; 
    std::uint64_t order_id;             
    std::int64_t price;                 
    std::uint32_t quantity;             
    Side side;                          
    MessageType type;                   
    char symbol[8];                     
    std::uint8_t _padding[18];
};

static_assert(sizeof(MarketMessage) == 64, "MarketMessage must be exactly 64 bytes (one cache line)");
static_assert(alignof(MarketMessage) == 64, "MarketMessage must be cache-line aligned");
static_assert(std::is_trivially_copyable_v<MarketMessage>, "MarketMessage must be trivially copyable");

inline std::uint64_t get_timestamp_ns() noexcept {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

}
