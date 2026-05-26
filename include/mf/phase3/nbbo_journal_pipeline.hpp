#pragma once

#include <cstdint>
#include <span>
#include <string>

#include "mf/core/types.hpp"

namespace mf::phase3 {

struct NbboJournalPipelineStats {
  std::uint64_t book_events_in{0};
  std::uint64_t nbbo_emitted{0};
  std::uint64_t top_changes{0};
  std::uint32_t journal_crc{0};
};

class NbboJournalPipeline {
 public:
  bool replay_to_journal(std::span<const mf::core::BookEvent> events, const std::string& out_path, NbboJournalPipelineStats& stats);
};

}  // namespace mf::phase3
