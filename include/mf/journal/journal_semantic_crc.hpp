#pragma once

#include <cstdint>
#include <string>

#include "mf/core/book_event_crc.hpp"
#include "mf/journal/journal_reader.hpp"

namespace mf::journal {

struct JournalSemanticStats {
  std::uint64_t records{0};
  std::uint32_t crc{0};
  std::uint64_t crc_failures{0};
};

[[nodiscard]] inline bool compute_semantic_crc(const std::string& path, JournalSemanticStats& out) {
  JournalReader reader;
  if (!reader.open(path)) {
    return false;
  }
  mf::core::BookEvent ev{};
  std::uint64_t ingest_ts_ns = 0;
  std::uint64_t seq = 0;
  out = {};
  while (reader.next(ev, ingest_ts_ns, seq)) {
    (void)ingest_ts_ns;
    (void)seq;
    mf::core::update_crc_from_book_event(out.crc, ev);
    ++out.records;
  }
  out.crc_failures = reader.stats().crc_failures;
  return out.crc_failures == 0;
}

}  // namespace mf::journal
