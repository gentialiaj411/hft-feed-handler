#include "mf/proto/mdp3/cert_bin_reader.hpp"

#include <cstring>

namespace mf::proto::mdp3 {

CertBinReader::CertBinReader(std::span<const std::byte> data) noexcept : data_(data) {}

bool CertBinReader::next(std::span<const std::byte>& udp_payload) noexcept {
  for (;;) {
    if (offset_ + 10 > data_.size()) {
      return false;
    }
    offset_ += 8;
    if (offset_ + 2 > data_.size()) {
      return false;
    }
    std::uint16_t block_size = 0;
    const auto* p = reinterpret_cast<const std::uint8_t*>(data_.data() + offset_);
    block_size = static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8U) | p[1]);
    offset_ += 2;
    if (block_size == 0) {
      continue;
    }
    if (offset_ + block_size > data_.size()) {
      return false;
    }
    udp_payload = data_.subspan(offset_, block_size);
    offset_ += block_size;
    return true;
  }
}

}  // namespace mf::proto::mdp3
