#include "mf/journal/journaling_sink.hpp"

namespace mf::journal {

void JournalingSink::on_merged_event(const mf::core::BookEvent& ev) noexcept {
  if (writer_ != nullptr) {
    writer_->append(ev, ev.ingest_ts_ns);
  }
  if (downstream_ != nullptr) {
    downstream_->on_merged_event(ev);
  }
}

void JournalingIngestSink::operator()(mf::phase2::FeedSide side, const mf::core::BookEvent& ev) const noexcept {
  if (writer_ != nullptr) {
    writer_->append(ev, ev.ingest_ts_ns);
  }
  if (downstream_) {
    downstream_(side, ev);
  }
}

}  // namespace mf::journal
