#include "../src/order_book.h"
#include "../src/market_data.h"

#include <cassert>
#include <iostream>

int main() {
    market::OrderBook book;

    market::OrderAdd add{};
    add.header.msg_type = market::MSG_ORDER_ADD;
    add.header.msg_len = static_cast<uint16_t>(sizeof(market::OrderAdd));
    add.header.sequence_num = 1;
    add.order_id = 10;
    add.symbol_id = 55;
    add.price = 1'000'000;
    add.size = 100;
    add.side = 'B';

    book.on_order_add(add);
    assert(book.best_bid() == 1'000'000);

    market::OrderCancel cancel{};
    cancel.header.msg_type = market::MSG_ORDER_CANCEL;
    cancel.header.msg_len = static_cast<uint16_t>(sizeof(market::OrderCancel));
    cancel.header.sequence_num = 2;
    cancel.order_id = 10;
    cancel.symbol_id = 55;

    book.on_order_cancel(cancel);
    assert(book.best_bid() == 0);

    std::cout << "test_order_book: OK\n";
    return 0;
}
