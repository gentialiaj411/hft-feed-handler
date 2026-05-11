#pragma once

#include "market_data.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <unordered_map>

namespace market {

struct Order {
    uint64_t order_id{};
    uint32_t symbol_id{};
    int64_t price{};
    uint32_t size{};
    char side{0};
};

class OrderBook {
public:

    void on_order_add(const OrderAdd& msg);

    void on_order_cancel(const OrderCancel& msg);

    void on_quote(const Quote& msg);

    int64_t best_bid() const;

    int64_t best_ask() const;

    int64_t spread() const;

    void print_top_levels(int n = 5) const;

private:

    std::map<int64_t, uint32_t, std::greater<>> bids_;

    std::map<int64_t, uint32_t> asks_;

    std::unordered_map<uint64_t, Order> orders_;
};

}

}
