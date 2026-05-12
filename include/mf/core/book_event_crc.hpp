#pragma once

#include <cstddef>
#include <cstdint>

#include "mf/core/crc32.hpp"
#include "mf/core/types.hpp"

namespace mf::core {

static_assert(
    sizeof(BookEvent) >= 64 && sizeof(BookEvent) <= 512,
    "BookEvent layout changed; update CRC field coverage review.");

inline void update_crc_from_book_event(std::uint32_t& crc, const BookEvent& ev) {
  crc = crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.venue), sizeof(ev.venue));
  crc = crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.type), sizeof(ev.type));
  crc = crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.sequence), sizeof(ev.sequence));
  crc = crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.exchange_ts_ns), sizeof(ev.exchange_ts_ns));
  crc = crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.symbol), sizeof(ev.symbol));
  crc = crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.order_id), sizeof(ev.order_id));
  crc = crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.qty), sizeof(ev.qty));
  crc = crc32_update(crc, reinterpret_cast<const std::byte*>(&ev.price), sizeof(ev.price));
}

}  // namespace mf::core
