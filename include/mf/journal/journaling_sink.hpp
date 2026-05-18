#pragma once

#include <functional>

#include "mf/journal/journal_writer.hpp"
#include "mf/phase2/ab_arbiter.hpp"
#include "mf/phase2/pipeline.hpp"

namespace mf::journal {

class JournalingSink final : public mf::phase2::IMergedEventSink {
 public:
  JournalingSink(JournalWriter* writer, mf::phase2::IMergedEventSink* downstream)
      : writer_(writer), downstream_(downstream) {}

  void on_merged_event(const mf::core::BookEvent& ev) noexcept override;

 private:
  JournalWriter* writer_{nullptr};
  mf::phase2::IMergedEventSink* downstream_{nullptr};
};

class JournalingIngestSink {
 public:
  using SinkFn = std::function<void(mf::phase2::FeedSide, const mf::core::BookEvent&)>;

  JournalingIngestSink(JournalWriter* writer, SinkFn downstream)
      : writer_(writer), downstream_(std::move(downstream)) {}

  void operator()(mf::phase2::FeedSide side, const mf::core::BookEvent& ev) const noexcept;

 private:
  JournalWriter* writer_{nullptr};
  SinkFn downstream_{};
};

}  // namespace mf::journal
