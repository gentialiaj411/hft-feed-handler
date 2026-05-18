#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "mf/core/types.hpp"

namespace mf::research {

struct EventStoreStats {
  std::uint64_t records{0};
  std::uint32_t crc{0};
  std::uint64_t first_exchange_ts_ns{0};
  std::uint64_t last_exchange_ts_ns{0};
  std::uint64_t crc_failures{0};
};

class IEventConsumer {
 public:
  virtual ~IEventConsumer() = default;
  virtual void on_event(const mf::core::BookEvent& ev) = 0;
};

class EventStore {
 public:
  explicit EventStore(std::string path);

  [[nodiscard]] const std::string& path() const noexcept { return path_; }

  bool append(std::span<const mf::core::BookEvent> events, bool fsync_on_close = false) const;
  bool replay(IEventConsumer& consumer, EventStoreStats* stats = nullptr) const;
  [[nodiscard]] std::vector<mf::core::BookEvent> load_all(EventStoreStats* stats = nullptr) const;

 private:
  std::string path_;
};

}  // namespace mf::research
