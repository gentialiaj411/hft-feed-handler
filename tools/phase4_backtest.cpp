#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "mf/phase4/backtest_runner.hpp"

namespace {

mf::core::BookEvent make_ev(
    mf::core::EventType type,
    mf::core::Side side,
    std::uint32_t price,
    std::uint32_t qty,
    std::uint64_t ts,
    std::uint64_t order_id = 0) {
  mf::core::BookEvent e{};
  e.venue = mf::core::Venue::Nasdaq;
  e.type = type;
  e.side = side;
  e.price = price;
  e.qty = qty;
  e.exchange_ts_ns = ts;
  e.ingest_ts_ns = ts;
  e.order_id = order_id;
  e.symbol.bytes = {'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
  return e;
}

std::string utc_stamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
  return oss.str();
}

}  // namespace

int main() {
  std::vector<mf::core::BookEvent> tape;
  tape.push_back(make_ev(mf::core::EventType::Add, mf::core::Side::Buy, 100, 200, 1, 1));
  tape.push_back(make_ev(mf::core::EventType::Add, mf::core::Side::Sell, 102, 200, 2, 2));
  tape.push_back(make_ev(mf::core::EventType::Trade, mf::core::Side::Sell, 100, 120, 3));
  tape.push_back(make_ev(mf::core::EventType::Trade, mf::core::Side::Buy, 102, 120, 4));

  mf::phase4::BacktestRunner runner;
  const auto rep = runner.run(tape);

  std::cout << "phase4_backtest_report\n";
  std::cout << "metric               value\n";
  std::cout << "realized_pnl         " << rep.realized_pnl << "\n";
  std::cout << "unrealized_pnl       " << rep.unrealized_pnl << "\n";
  std::cout << "total_pnl            " << rep.total_pnl << "\n";
  std::cout << "sharpe               " << rep.sharpe << "\n";
  std::cout << "fill_ratio           " << rep.fill_ratio << "\n";
  std::cout << "max_drawdown         " << rep.max_drawdown << "\n";
  std::cout << "turnover             " << rep.turnover << "\n";
  std::cout << "fills                " << rep.fills << "\n";
  std::cout << "submitted_orders     " << rep.submitted_orders << "\n";

  std::filesystem::create_directories("bench/results");
  const std::string out = "bench/results/phase4_backtest_" + utc_stamp() + ".json";
  std::ofstream ofs(out);
  ofs << "{\n"
      << "  \"realized_pnl\": " << rep.realized_pnl << ",\n"
      << "  \"unrealized_pnl\": " << rep.unrealized_pnl << ",\n"
      << "  \"total_pnl\": " << rep.total_pnl << ",\n"
      << "  \"sharpe\": " << rep.sharpe << ",\n"
      << "  \"fill_ratio\": " << rep.fill_ratio << ",\n"
      << "  \"max_drawdown\": " << rep.max_drawdown << ",\n"
      << "  \"turnover\": " << rep.turnover << ",\n"
      << "  \"fills\": " << rep.fills << ",\n"
      << "  \"submitted_orders\": " << rep.submitted_orders << "\n"
      << "}\n";
  std::cout << "json_path            " << out << "\n";
  return 0;
}
