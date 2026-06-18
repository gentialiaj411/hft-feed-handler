#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mf/live/bitfinex/wire_types.hpp"

namespace mf::live::bitfinex {

struct SnapshotSidecar {
  std::uint32_t version{1};
  std::string symbol{};
  std::uint64_t cut_sequence{0};
  std::uint64_t exchange_ts_ns{0};
  std::vector<BookRow> rows{};
};

[[nodiscard]] std::string sidecar_path_for_journal(const std::string& journal_path) noexcept;
[[nodiscard]] bool write_sidecar(const std::string& path, const SnapshotSidecar& sidecar) noexcept;
[[nodiscard]] bool read_sidecar(const std::string& path, SnapshotSidecar& out) noexcept;

}  // namespace mf::live::bitfinex
