#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mf/journal/nbbo_event.hpp"

namespace mf::journal {

class NbboJournalWriter {
 public:
  explicit NbboJournalWriter(std::size_t buffer_bytes = (1U << 20U), bool fsync_on_flush = false);
  ~NbboJournalWriter();

  bool open(const std::string& path);
  void append(const NbboEvent& ev) noexcept;
  void flush();
  void close();

 private:
  bool write_header_if_needed();
  bool validate_existing_header();
  void flush_active_buffer() noexcept;

  int fd_{-1};
  std::vector<std::byte> buf_a_{};
  std::vector<std::byte> buf_b_{};
  std::byte* active_{nullptr};
  std::size_t active_used_{0};
  std::size_t buffer_bytes_{0};
  std::uint64_t next_seq_{1};
  bool fsync_on_flush_{false};
};

}  // namespace mf::journal
