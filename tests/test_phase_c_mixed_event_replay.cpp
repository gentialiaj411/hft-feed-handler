#include <cassert>
#include <cstdio>
#include <vector>

#include "mf/journal/journal_reader.hpp"
#include "mf/journal/journal_writer.hpp"
#include "mf/phase2/pipeline.hpp"
#include "mf/phase4/backtest_runner.hpp"

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {

struct CollectSink final : mf::phase2::IMergedEventSink {
  std::vector<mf::core::BookEvent> merged{};
  void on_merged_event(const mf::core::BookEvent& ev) noexcept override { merged.push_back(ev); }
};

mf::core::BookEvent ev(
    mf::core::EventType type,
    std::uint64_t seq,
    std::uint64_t ts,
    mf::core::Side side = mf::core::Side::Unknown,
    std::uint32_t price = 0,
    std::uint32_t qty = 0,
    std::uint64_t order_id = 0,
    std::uint64_t ref_order_id = 0) {
  mf::core::BookEvent out{};
  out.venue = mf::core::Venue::Nasdaq;
  out.type = type;
  out.sequence = seq;
  out.exchange_ts_ns = ts;
  out.ingest_ts_ns = ts + 1000;
  out.side = side;
  out.price = price;
  out.qty = qty;
  out.order_id = order_id;
  out.reference_order_id = ref_order_id;
  out.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  return out;
}

}  // namespace

int main() {
#if !defined(__linux__)
  std::printf("SKIP: mixed-event journal replay test is Linux-only\n");
  return 0;
#else
  char path[] = "/tmp/mfjn_mixed_XXXXXX";
  const int fd = ::mkstemp(path);
  assert(fd >= 0);
  ::close(fd);

  const std::vector<mf::core::BookEvent> src{
      ev(mf::core::EventType::System, 1, 1),
      ev(mf::core::EventType::StockDirectory, 2, 2),
      ev(mf::core::EventType::Unknown, 3, 3),
      ev(mf::core::EventType::Add, 4, 4, mf::core::Side::Buy, 100, 200, 101),
      ev(mf::core::EventType::Add, 5, 5, mf::core::Side::Sell, 102, 200, 102),
      ev(mf::core::EventType::Replace, 6, 6, mf::core::Side::Buy, 101, 150, 201, 101),
      ev(mf::core::EventType::Trade, 7, 7, mf::core::Side::Sell, 101, 80),
      ev(mf::core::EventType::CrossTrade, 8, 8, mf::core::Side::Unknown, 101, 50),
      ev(mf::core::EventType::Imbalance, 9, 9, mf::core::Side::Unknown, 0, 1000),
      ev(mf::core::EventType::Delete, 10, 10, mf::core::Side::Unknown, 0, 0, 102),
  };

  mf::journal::JournalWriter writer(1U << 16U, true);
  assert(writer.open(path));
  for (const auto& item : src) {
    writer.append(item, item.ingest_ts_ns);
  }
  writer.close();

  mf::journal::JournalReader reader;
  assert(reader.open(path));
  mf::phase2::Pipeline pipeline(1024, 1U << 16U);
  CollectSink sink{};
  mf::core::BookEvent item{};
  std::uint64_t ingest_ts = 0;
  std::uint64_t journal_seq = 0;
  while (reader.next(item, ingest_ts, journal_seq)) {
    (void)journal_seq;
    item.ingest_ts_ns = ingest_ts;
    pipeline.on_event(item);
  }
  assert(!reader.had_error());
  pipeline.finalize(&sink);

  assert(reader.stats().records_read == src.size());
  assert(reader.stats().crc_failures == 0);
  assert(sink.merged.size() == src.size());

  mf::phase4::BacktestRunner runner;
  const auto report = runner.run(sink.merged);
  assert(report.submitted_orders > 0);

  ::unlink(path);
  return 0;
#endif
}
