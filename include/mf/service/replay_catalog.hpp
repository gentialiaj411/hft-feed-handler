#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "mf/journal/nbbo_event.hpp"

namespace mf::service {

// Metadata loaded at server startup from mmap'd journals (read-only).
struct ReplayCatalog {
  std::string book_journal_path;
  std::string nbbo_journal_path;
  std::uint64_t book_record_count{0};
  std::uint64_t nbbo_record_count{0};
  std::uint32_t nbbo_journal_crc{0};
  std::chrono::steady_clock::time_point started_at{std::chrono::steady_clock::now()};

  // NBBO rows kept in memory for point lookups (scan-on-stream still used for StreamNbbo).
  std::vector<mf::journal::NbboEvent> nbbo_events;

  // Opens journals, counts book records, loads NBBO journal when path non-empty.
  bool load(const std::string& book_path, const std::string& nbbo_path);

  [[nodiscard]] std::int64_t uptime_seconds() const noexcept;
};

}  // namespace mf::service
