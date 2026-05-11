
#include "order_book.h"

namespace market {

void OrderBook::on_order_add(const OrderAdd& msg) {

    Order order{msg.order_id, msg.symbol_id, msg.price, msg.size, msg.side};

    orders_[msg.order_id] = order;

    if (msg.side == 'B') {

        bids_[msg.price] += msg.size;
    } else {

        asks_[msg.price] += msg.size;
    }
}

void OrderBook::on_order_cancel(const OrderCancel& msg) {

    const auto it = orders_.find(msg.order_id);
    if (it == orders_.end()) {

        return;
    }

    const auto& order = it->second;

    if (order.side == 'B') {

        auto book_it = bids_.find(order.price);
        if (book_it != bids_.end()) {
            if (book_it->second > order.size) {

                book_it->second -= order.size;
            } else {

                bids_.erase(book_it);
            }
        }
    } else {

        auto book_it = asks_.find(order.price);
        if (book_it != asks_.end()) {
            if (book_it->second > order.size) {
                book_it->second -= order.size;
            } else {
                asks_.erase(book_it);
            }
        }
    }

    orders_.erase(it);
}

void OrderBook::on_quote(const Quote& msg) {

    bids_[msg.bid_price] = msg.bid_size;
    asks_[msg.ask_price] = msg.ask_size;
}

int64_t OrderBook::best_bid() const {
    if (bids_.empty()) {
        return 0;
    }

    return bids_.begin()->first;
}

int64_t OrderBook::best_ask() const {
    if (asks_.empty()) {
        return 0;
    }

    return asks_.begin()->first;
}

int64_t OrderBook::spread() const {
    const auto bid = best_bid();
    const auto ask = best_ask();

    if (bid == 0 || ask == 0) {
        return 0;
    }

    return ask - bid;
}

void OrderBook::print_top_levels(int n) const {
    std::cout << "Top " << n << " Bids:\n";
    int count = 0;

    for (const auto& [price, size] : bids_) {
        if (count++ >= n) {
            break;
        }
        std::cout << "  " << price << " : " << size << "\n";
    }

    std::cout << "Top " << n << " Asks:\n";
    count = 0;

    for (const auto& [price, size] : asks_) {
        if (count++ >= n) {
            break;
        }
        std::cout << "  " << price << " : " << size << "\n";
    }
}

}
