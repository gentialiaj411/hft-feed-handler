#include "../src/message_parser.h"
#include "../src/market_data.h"

#include <cassert>
#include <iostream>

int main() {
    market::MessageParser parser;
    market::RawMessage raw{};
    raw.len = sizeof(market::Quote);
    auto* quote = reinterpret_cast<market::Quote*>(raw.payload.data());
    quote->header.msg_type = market::MSG_QUOTE;
    quote->header.msg_len = static_cast<uint16_t>(sizeof(market::Quote));
    quote->header.sequence_num = 1;

    const auto* header = parser.parse(raw);
    assert(header != nullptr);
    assert(parser.sequence_gaps() == 0);

    raw.len = 4;
    assert(parser.parse(raw) == nullptr);
    assert(parser.invalid_messages() == 1);

    std::cout << "test_parser: OK\n";
    return 0;
}
