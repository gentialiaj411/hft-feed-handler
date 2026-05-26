#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace mf::proto::mdp3 {

// EPAM CME certification feed dump: [u64 timestamp][u16 be length][udp payload]...
struct CertBinReader {
  static constexpr std::size_t kInitOffset = 10;

  explicit CertBinReader(std::span<const std::byte> data) noexcept;

  [[nodiscard]] bool next(std::span<const std::byte>& udp_payload) noexcept;

 private:
  std::span<const std::byte> data_;
  std::size_t offset_{kInitOffset};
};

}  // namespace mf::proto::mdp3
