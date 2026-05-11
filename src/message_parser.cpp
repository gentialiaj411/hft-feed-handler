
#include "message_parser.h"

#include <cstddef>

namespace market {

const MessageHeader* MessageParser::parse(const RawMessage& raw) {

    if (raw.len < sizeof(MessageHeader)) {
        ++invalid_;
        return nullptr;
    }

    const auto* header = reinterpret_cast<const MessageHeader*>(raw.payload.data());

    if (header->msg_len == 0 || static_cast<size_t>(header->msg_len) > raw.len) {
        ++invalid_;
        return nullptr;
    }

    const uint64_t expected_len = compute_expected_len(header);
    if (expected_len == 0 || expected_len != header->msg_len) {
        ++invalid_;
        return nullptr;
    }

    if (last_sequence_ != 0 && header->sequence_num != last_sequence_ + 1) {

        gaps_ += header->sequence_num - last_sequence_ - 1;

    }
    last_sequence_ = header->sequence_num;

    return header;
}

uint64_t MessageParser::compute_expected_len(const MessageHeader* header) const {

    switch (header->msg_type) {
        case MSG_QUOTE:
            return sizeof(Quote);
        case MSG_TRADE:
            return sizeof(Trade);
        case MSG_ORDER_ADD:
            return sizeof(OrderAdd);
        case MSG_ORDER_CANCEL:
            return sizeof(OrderCancel);
        default:
            return 0;
    }
}

uint64_t MessageParser::sequence_gaps() const {
    return gaps_;
}

uint64_t MessageParser::invalid_messages() const {
    return invalid_;
}

}
