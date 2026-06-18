#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mf::live::bitfinex {

struct BookRow {
  std::uint64_t order_id{0};
  double price{0.0};
  double amount{0.0};
};

struct SnapshotFrame {
  std::int64_t channel_id{0};
  std::vector<BookRow> rows{};
  std::uint64_t sequence{0};
};

struct UpdateFrame {
  std::int64_t channel_id{0};
  BookRow row{};
  std::uint64_t sequence{0};
};

enum class ParsedKind { None, Snapshot, Update, Heartbeat, Control };

struct ParsedFrame {
  ParsedKind kind{ParsedKind::None};
  SnapshotFrame snapshot{};
  UpdateFrame update{};
  std::string control_event{};
};

[[nodiscard]] bool parse_frame(const std::string& json, ParsedFrame& out) noexcept;

}  // namespace mf::live::bitfinex
