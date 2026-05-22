#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "mf/bench/run_metadata.hpp"
#include "mf/bench/tsc_clock.hpp"
#include "mf/core/types.hpp"
#include "mf/phase3/intrusive_order_book.hpp"
#include "mf/phase3/order_book_engine.hpp"

namespace {

struct Config {
  std::uint64_t cycles{200'000};
  std::uint64_t warmup_cycles{10'000};
  std::string out_dir{"bench/results"};
};

class NaiveMapOrderBook {
 public:
  struct ApplyResult {};

  ApplyResult on_event(const mf::core::BookEvent& ev) {
    const auto key = order_key(ev.venue, ev.order_id);
    auto& book = books_[static_cast<std::size_t>(static_cast<std::uint8_t>(ev.venue))][ev.symbol.as_u64()];
    if (ev.type == mf::core::EventType::Add || ev.type == mf::core::EventType::AddMpid) {
      if (ev.side == mf::core::Side::Buy) {
        book.bids[ev.price] += ev.qty;
      } else if (ev.side == mf::core::Side::Sell) {
        book.asks[ev.price] += ev.qty;
      }
      orders_[key] = OrderRef{ev.venue, ev.symbol.as_u64(), ev.side, ev.price, ev.qty};
    } else if (
        ev.type == mf::core::EventType::Execute ||
        ev.type == mf::core::EventType::ExecutePrice ||
        ev.type == mf::core::EventType::Cancel ||
        ev.type == mf::core::EventType::Delete) {
      reduce(key, ev.qty, ev.type == mf::core::EventType::Delete);
    } else if (ev.type == mf::core::EventType::Replace) {
      auto side = ev.side;
      const auto old_key = order_key(ev.venue, ev.reference_order_id);
      if (auto it = orders_.find(old_key); it != orders_.end()) {
        if (side == mf::core::Side::Unknown) {
          side = it->second.side;
        }
        reduce(old_key, 0, true);
      }
      auto add_ev = ev;
      add_ev.type = mf::core::EventType::Add;
      add_ev.side = side;
      on_event(add_ev);
    }
    return {};
  }

 private:
  struct OrderRef {
    mf::core::Venue venue{mf::core::Venue::Nasdaq};
    std::uint64_t symbol{0};
    mf::core::Side side{mf::core::Side::Unknown};
    std::uint32_t price{0};
    std::uint32_t qty{0};
  };
  struct Book {
    std::map<std::uint32_t, std::uint64_t, std::greater<std::uint32_t>> bids{};
    std::map<std::uint32_t, std::uint64_t> asks{};
  };

  static std::uint64_t order_key(mf::core::Venue venue, std::uint64_t order_id) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint8_t>(venue)) << 56U) ^ order_id;
  }

  void reduce(std::uint64_t key, std::uint32_t qty, bool delete_all) {
    const auto it = orders_.find(key);
    if (it == orders_.end()) return;
    auto& ref = it->second;
    auto& book = books_[static_cast<std::size_t>(static_cast<std::uint8_t>(ref.venue))][ref.symbol];
    const auto dec = (delete_all || qty == 0 || qty > ref.qty) ? ref.qty : qty;
    if (ref.side == mf::core::Side::Buy) {
      auto level = book.bids.find(ref.price);
      if (level != book.bids.end()) {
        level->second = level->second > dec ? level->second - dec : 0;
        if (level->second == 0) book.bids.erase(level);
      }
    } else if (ref.side == mf::core::Side::Sell) {
      auto level = book.asks.find(ref.price);
      if (level != book.asks.end()) {
        level->second = level->second > dec ? level->second - dec : 0;
        if (level->second == 0) book.asks.erase(level);
      }
    }
    if (delete_all || dec >= ref.qty) {
      orders_.erase(it);
    } else {
      ref.qty -= dec;
    }
  }

  std::array<std::map<std::uint64_t, Book>, 3> books_{};
  std::map<std::uint64_t, OrderRef> orders_{};
};

Config parse_args(int argc, char** argv) {
  Config cfg{};
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--cycles" && i + 1 < argc) {
      cfg.cycles = std::stoull(argv[++i]);
    } else if (arg == "--warmup-cycles" && i + 1 < argc) {
      cfg.warmup_cycles = std::stoull(argv[++i]);
    } else if (arg == "--out-dir" && i + 1 < argc) {
      cfg.out_dir = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "usage: book_engine_bench [--cycles N] [--warmup-cycles N] [--out-dir DIR]\n";
      std::exit(0);
    }
  }
  return cfg;
}

std::string stamp() {
  const auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  char out[64]{};
  std::strftime(out, sizeof(out), "%Y%m%dT%H%M%SZ", &tm);
  return out;
}

mf::core::SymbolKey symbol_for(std::uint64_t idx) {
  mf::core::SymbolKey s{};
  const char suffix = static_cast<char>('1' + static_cast<char>(idx % 8U));
  s.bytes = {'S', 'Y', 'M', 'B', '0', '0', '0', suffix};
  return s;
}

std::vector<mf::core::BookEvent> make_events(std::uint64_t cycles) {
  std::vector<mf::core::BookEvent> events;
  events.reserve(static_cast<std::size_t>(cycles * 5U));
  std::uint64_t seq = 1;
  for (std::uint64_t cycle = 0; cycle < cycles; ++cycle) {
    const std::uint64_t sym = cycle & 7U;
    const auto venue = static_cast<mf::core::Venue>(cycle % 3U);
    const auto symbol = symbol_for(sym);
    const std::uint64_t base = 1'000'000ULL + cycle * 10ULL;
    const std::uint32_t price = 10'000U + static_cast<std::uint32_t>((cycle + sym) & 63U);

    mf::core::BookEvent add_bid{};
    add_bid.venue = venue;
    add_bid.type = mf::core::EventType::Add;
    add_bid.sequence = seq++;
    add_bid.exchange_ts_ns = add_bid.sequence;
    add_bid.symbol = symbol;
    add_bid.side = mf::core::Side::Buy;
    add_bid.price = price;
    add_bid.qty = 100;
    add_bid.order_id = base + 1U;
    events.push_back(add_bid);

    auto add_ask = add_bid;
    add_ask.sequence = seq++;
    add_ask.exchange_ts_ns = add_ask.sequence;
    add_ask.side = mf::core::Side::Sell;
    add_ask.price = price + 20U;
    add_ask.qty = 120;
    add_ask.order_id = base + 2U;
    events.push_back(add_ask);

    auto exec_bid = add_bid;
    exec_bid.type = mf::core::EventType::Execute;
    exec_bid.sequence = seq++;
    exec_bid.exchange_ts_ns = exec_bid.sequence;
    exec_bid.side = mf::core::Side::Unknown;
    exec_bid.price = 0;
    exec_bid.qty = 40;
    events.push_back(exec_bid);

    auto cancel_ask = add_ask;
    cancel_ask.type = mf::core::EventType::Cancel;
    cancel_ask.sequence = seq++;
    cancel_ask.exchange_ts_ns = cancel_ask.sequence;
    cancel_ask.side = mf::core::Side::Unknown;
    cancel_ask.price = 0;
    cancel_ask.qty = 30;
    events.push_back(cancel_ask);

    auto replace_bid = add_bid;
    replace_bid.type = mf::core::EventType::Replace;
    replace_bid.sequence = seq++;
    replace_bid.exchange_ts_ns = replace_bid.sequence;
    replace_bid.side = mf::core::Side::Buy;
    replace_bid.price = price + 1U;
    replace_bid.qty = 50;
    replace_bid.order_id = base + 3U;
    replace_bid.reference_order_id = base + 1U;
    events.push_back(replace_bid);
  }
  return events;
}

template <typename Book>
double run_book(Book& book, const std::vector<mf::core::BookEvent>& events, double ticks_per_ns) {
  const auto t0 = mf::bench::tsc_now();
  for (const auto& ev : events) {
    (void)book.on_event(ev);
  }
  const auto t1 = mf::bench::tsc_now();
  const double elapsed_ns = static_cast<double>(mf::bench::ticks_to_ns(t1 - t0, ticks_per_ns));
  return (elapsed_ns > 0.0) ? (static_cast<double>(events.size()) * 1'000'000'000.0 / elapsed_ns) : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
  const auto cfg = parse_args(argc, argv);
  const auto warmup = make_events(cfg.warmup_cycles);
  const auto events = make_events(cfg.cycles);
  const double ticks_per_ns = mf::bench::calibrate_ticks_per_ns();
  auto meta = mf::bench::capture_run_metadata(argc, argv);

  {
    NaiveMapOrderBook book;
    (void)run_book(book, warmup, ticks_per_ns);
  }
  {
    mf::phase3::OrderBookEngine book;
    (void)run_book(book, warmup, ticks_per_ns);
  }
  {
    mf::phase3::IntrusiveOrderBook book;
    (void)run_book(book, warmup, ticks_per_ns);
  }

  NaiveMapOrderBook naive;
  mf::phase3::OrderBookEngine reference;
  mf::phase3::IntrusiveOrderBook intrusive;
  const double naive_ops = run_book(naive, events, ticks_per_ns);
  const double reference_ops = run_book(reference, events, ticks_per_ns);
  const double intrusive_ops = run_book(intrusive, events, ticks_per_ns);
  const double intrusive_vs_naive = naive_ops > 0.0 ? intrusive_ops / naive_ops : 0.0;
  const double reference_vs_naive = naive_ops > 0.0 ? reference_ops / naive_ops : 0.0;
  const double intrusive_vs_reference = reference_ops > 0.0 ? intrusive_ops / reference_ops : 0.0;

  std::filesystem::create_directories(cfg.out_dir);
  const auto path = cfg.out_dir + "/book_throughput_" + stamp() + ".md";
  std::ofstream os(path);
  os << "# Book Throughput\n\n";
  os << "- cycles: " << cfg.cycles << "\n";
  os << "- events: " << events.size() << "\n";
  os << "- warmup_cycles: " << cfg.warmup_cycles << "\n";
  os << "- timing_source: RDTSCP when available, steady_clock fallback otherwise\n";
  os << "- ticks_per_ns: " << ticks_per_ns << "\n\n";
  os << "| engine | ops_per_sec |\n";
  os << "|---|---:|\n";
  os << "| naive_std_map_order_book | " << naive_ops << " |\n";
  os << "| current_aggregate_order_book | " << reference_ops << " |\n";
  os << "| intrusive_slab_order_book | " << intrusive_ops << " |\n";
  os << "| current_vs_naive | " << reference_vs_naive << "x |\n";
  os << "| intrusive_vs_naive | " << intrusive_vs_naive << "x |\n";
  os << "| intrusive_vs_current | " << intrusive_vs_reference << "x |\n\n";
  os << "Methodology: same synthetic add/execute/cancel/replace lifecycle tape, warmup discarded, single-thread hot loop.\n";
  os << "Note: the intrusive engine maintains per-order queues and slab-backed nodes; the current aggregate engine remains faster on this aggregate-only workload.\n";
  os << "Metadata: " << mf::bench::run_metadata_to_json(meta) << "\n";
  std::cout << "book_report=" << path << "\n";
  std::cout << "naive_ops_per_sec=" << naive_ops << "\n";
  std::cout << "reference_ops_per_sec=" << reference_ops << "\n";
  std::cout << "intrusive_ops_per_sec=" << intrusive_ops << "\n";
  std::cout << "intrusive_vs_naive=" << intrusive_vs_naive << "\n";
  std::cout << "intrusive_vs_current=" << intrusive_vs_reference << "\n";
  return 0;
}
