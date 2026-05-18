#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/journal/journal_format.hpp"

namespace mf::journal {

class JournalWriter {
 public:
  explicit JournalWriter(std::size_t buffer_bytes = (1U << 20U), bool fsync_on_flush = false);
  ~JournalWriter();

  bool open(const std::string& path);
  void append(const mf::core::BookEvent& ev, std::uint64_t ingest_ts_ns) noexcept;
  // fsync trade-off (v1):
  // 1) fsync on every flush gives stronger durability but can dominate latency.
  // 2) hot paths usually run with fsync disabled and rely on periodic/manual flush.
  // 3) close() still flushes buffers; optional fsync-on-close can be enabled.
  // 4) when disabled, kernel page cache may delay persistence after process crash.
  // 5) when enabled, throughput depends on storage and journal append cadence.
  // 6) recommended: disabled in capture path, enabled at controlled shutdown.
  // 7) for stricter semantics, users can call flush() at bounded intervals.
  // 8) this class keeps append allocation-free regardless of fsync mode.
  // 9) v1 does not batch fsync across multiple journal files.
  // 10) tune buffer size + flush cadence based on loss tolerance.
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
