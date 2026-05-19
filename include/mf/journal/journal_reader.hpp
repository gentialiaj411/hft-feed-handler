#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "mf/core/types.hpp"

namespace mf::journal {

struct JournalReaderStats {
  std::uint64_t records_read{0};
  std::uint64_t crc_failures{0};
  std::uint64_t bytes_read{0};
};

class JournalReader {
 public:
  JournalReader() = default;
  ~JournalReader();

  bool open(const std::string& path);
  bool next(mf::core::BookEvent& out, std::uint64_t& ingest_ts_ns, std::uint64_t& monotonic_seq);
  void close();
  [[nodiscard]] const JournalReaderStats& stats() const noexcept { return stats_; }
  [[nodiscard]] const char* error_reason() const noexcept { return error_reason_; }
  [[nodiscard]] std::size_t error_offset() const noexcept { return error_offset_; }
  [[nodiscard]] bool had_error() const noexcept { return had_error_; }

 private:
  void set_error(const char* reason, std::size_t offset) noexcept {
    error_reason_ = reason;
    error_offset_ = offset;
    had_error_ = true;
  }
  const std::byte* base_{nullptr};
  std::size_t size_{0};
  std::size_t off_{0};
  int fd_{-1};
  JournalReaderStats stats_{};
  const char* error_reason_{nullptr};
  std::size_t error_offset_{0};
  bool had_error_{false};
};

}  // namespace mf::journal
