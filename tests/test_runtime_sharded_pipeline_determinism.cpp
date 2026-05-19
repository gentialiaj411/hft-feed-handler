#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/runtime/sharded_pipeline.hpp"

namespace {

mf::core::SymbolKey symbol_from_index(std::uint64_t i) {
  static constexpr std::array<const char*, 8> kSymbols = {
      "AAPL", "MSFT", "NVDA", "AMZN", "GOOG", "META", "TSLA", "AMD "};
  const char* s = kSymbols[static_cast<std::size_t>(i % kSymbols.size())];
  mf::core::SymbolKey out{};
  out.bytes = {s[0], s[1], s[2], s[3], ' ', ' ', ' ', ' '};
  return out;
}

mf::core::BookEvent make_event(std::uint64_t i,
                               std::array<std::uint64_t, 3>& seq_by_venue,
                               std::array<std::uint64_t, 8>& ts_by_symbol) {
  const auto venue = static_cast<mf::core::Venue>(i % 3U);
  const std::size_t symbol_idx = static_cast<std::size_t>(i % 8U);

  mf::core::BookEvent ev{};
  ev.venue = venue;
  ev.type = (i % 6U < 2U) ? mf::core::EventType::Add : ((i % 6U < 4U) ? mf::core::EventType::Trade : mf::core::EventType::Delete);
  ev.sequence = ++seq_by_venue[static_cast<std::size_t>(static_cast<std::uint8_t>(venue))];
  ev.exchange_ts_ns = ++ts_by_symbol[symbol_idx];
  ev.ingest_ts_ns = ev.exchange_ts_ns + 100U;
  ev.symbol = symbol_from_index(i);
  ev.order_id = i + 1U;
  ev.reference_order_id = (ev.type == mf::core::EventType::Delete) ? ev.order_id : 0U;
  ev.match_id = i + 1000U;
  ev.side = (i % 2U == 0U) ? mf::core::Side::Buy : mf::core::Side::Sell;
  ev.qty = 50U + static_cast<std::uint32_t>(i % 11U);
  ev.price = 10'000U + static_cast<std::uint32_t>(i % 97U);
  ev.raw_type = static_cast<std::uint8_t>('A' + (i % 4U));
  return ev;
}

std::vector<mf::core::BookEvent> make_stream(std::size_t n) {
  std::vector<mf::core::BookEvent> events;
  events.reserve(n);
  std::array<std::uint64_t, 3> seq_by_venue = {0, 0, 0};
  std::array<std::uint64_t, 8> ts_by_symbol{};
  for (std::size_t i = 0; i < n; ++i) {
    events.push_back(make_event(static_cast<std::uint64_t>(i), seq_by_venue, ts_by_symbol));
  }
  return events;
}

std::uint32_t run_sharded_crc(const std::vector<mf::core::BookEvent>& events, std::size_t shards) {
  mf::runtime::ShardedPipeline<> pipeline({shards, 256, 8192});
  for (const auto& ev : events) {
    while (!pipeline.submit(ev)) {
    }
  }
  pipeline.close_input();
  pipeline.finalize();
  return pipeline.stats().reaggregated_crc;
}

void test_reaggregated_crc_matches_baseline_across_shards() {
  const auto events = make_stream(120000);
  const std::uint32_t baseline_crc = mf::runtime::deterministic_crc_for_events(events);
  assert(baseline_crc != 0U);

  for (const std::size_t shards : {1U, 2U, 4U, 8U}) {
    const std::uint32_t shard_crc = run_sharded_crc(events, shards);
    assert(shard_crc == baseline_crc);
  }
}

}  // namespace

int main() {
  test_reaggregated_crc_matches_baseline_across_shards();
  return 0;
}
