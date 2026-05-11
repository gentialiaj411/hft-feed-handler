#pragma once

#include "market_data.h"

#include <cstdint>

namespace market {

class MessageParser {
public:

    const MessageHeader* parse(const RawMessage& raw);

    template <typename T>
    const T* as(const MessageHeader* header) const {

        return reinterpret_cast<const T*>(header);
    }

    uint64_t sequence_gaps() const;

    uint64_t invalid_messages() const;

private:

    uint64_t compute_expected_len(const MessageHeader* header) const;

    uint32_t last_sequence_{0};
    uint64_t gaps_{0};
    uint64_t invalid_{0};
};

}

}
