#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace market {

enum MessageType : uint16_t {
    MSG_QUOTE = 1,
    MSG_TRADE = 2,
    MSG_ORDER_ADD = 3,
    MSG_ORDER_CANCEL = 4,
};

#pragma pack(push, 1)

struct MessageHeader {
    uint16_t msg_type;
    uint16_t msg_len;
    uint32_t sequence_num;
    uint64_t timestamp_ns;
};

struct Quote {
    MessageHeader header;
    uint32_t symbol_id;
    int64_t bid_price;
    int64_t ask_price;
    uint32_t bid_size;
    uint32_t ask_size;
};

struct Trade {
    MessageHeader header;
    uint32_t symbol_id;
    int64_t price;
    uint32_t size;
    char side;
    char padding[3];
};

struct OrderAdd {
    MessageHeader header;
    uint64_t order_id;
    uint32_t symbol_id;
    int64_t price;
    uint32_t size;
    char side;
    char padding[3];
};

struct OrderCancel {
    MessageHeader header;
    uint64_t order_id;
    uint32_t symbol_id;
};

#pragma pack(pop)

struct RawMessage {

    static constexpr size_t MaxPayload = 2048;

    std::array<char, MaxPayload> payload{};
    size_t len{0};
    uint64_t recv_timestamp_ns{0};
};

}
