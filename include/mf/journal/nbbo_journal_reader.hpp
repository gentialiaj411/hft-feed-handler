#pragma once

#include <cstdint>
#include <string>

#include "mf/journal/nbbo_event.hpp"

namespace mf::journal {

struct NbboJournalReaderStats {
  std::uint64_t records_read{0};
  std::uint64_t bytes_read{0};
  std::uint64_t crc_failures{0};
};

class NbboJournalReader {
 public:
  NbboJournalReader() = default;
  ~NbboJournalReader();

  bool open(const std::string& path);
  bool next(NbboEvent& out);
  void close();
  [[nodiscard]] const NbboJournalReaderStats& stats() const noexcept { return stats_; }
  [[nodiscard]] std::uint32_t payload_crc() const noexcept { return payload_crc_; }

 private:
  const std::byte* base_{nullptr};
  std::size_t size_{0};
  std::size_t off_{0};
  int fd_{-1};
  NbboJournalReaderStats stats_{};
  std::uint32_t payload_crc_{0};
};

}  // namespace mf::journal
