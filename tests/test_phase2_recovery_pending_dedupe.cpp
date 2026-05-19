#include <cassert>

#include "mf/phase2/pipeline.hpp"

namespace {

mf::core::BookEvent make_ev(std::uint64_t seq) {
  mf::core::BookEvent ev{};
  ev.venue = mf::core::Venue::Nasdaq;
  ev.type = mf::core::EventType::StockDirectory;
  ev.sequence = seq;
  ev.exchange_ts_ns = seq;
  ev.ingest_ts_ns = seq;
  ev.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  return ev;
}

}  // namespace

int main() {
  mf::phase2::Pipeline pipeline(1024, 1U << 16U);

  for (std::uint64_t seq = 1; seq <= 38; ++seq) {
    pipeline.on_event(make_ev(seq));
  }
  for (std::uint64_t seq = 41; seq <= 100; ++seq) {
    pipeline.on_event(make_ev(seq));
  }

  const auto& stats = pipeline.stats();
  assert(stats.accepted == 38);
  assert(stats.buffered_out_of_order == 60);
  assert(stats.recovery_reinjected == 0);
  assert(stats.dropped_publish_overflow == 0);
  return 0;
}
