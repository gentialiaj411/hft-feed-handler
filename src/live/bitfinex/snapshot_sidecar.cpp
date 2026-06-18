#include "mf/live/bitfinex/snapshot_sidecar.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace mf::live::bitfinex {

namespace {

std::string escape_json(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

}  // namespace

std::string sidecar_path_for_journal(const std::string& journal_path) noexcept {
  return journal_path + ".snapshot.json";
}

bool write_sidecar(const std::string& path, const SnapshotSidecar& sidecar) noexcept {
  std::ostringstream os;
  os << std::setprecision(17);
  os << "{\n";
  os << "  \"version\": " << sidecar.version << ",\n";
  os << "  \"symbol\": \"" << escape_json(sidecar.symbol) << "\",\n";
  os << "  \"cut_sequence\": " << sidecar.cut_sequence << ",\n";
  os << "  \"exchange_ts_ns\": " << sidecar.exchange_ts_ns << ",\n";
  os << "  \"rows\": [\n";
  for (std::size_t i = 0; i < sidecar.rows.size(); ++i) {
    const auto& r = sidecar.rows[i];
    os << "    {\"order_id\": " << r.order_id << ", \"price\": " << r.price
       << ", \"amount\": " << r.amount << "}";
    if (i + 1 < sidecar.rows.size()) os << ',';
    os << '\n';
  }
  os << "  ]\n}\n";
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  out << os.str();
  return static_cast<bool>(out);
}

bool read_sidecar(const std::string& path, SnapshotSidecar& out) noexcept {
  std::ifstream in(path);
  if (!in) return false;
  std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  out = {};
  out.version = 1;
  const auto sym_pos = json.find("\"symbol\"");
  if (sym_pos != std::string::npos) {
    const auto q1 = json.find('"', sym_pos + 8);
    const auto q2 = json.find('"', q1 + 1);
    if (q1 != std::string::npos && q2 != std::string::npos) {
      out.symbol = json.substr(q1 + 1, q2 - q1 - 1);
    }
  }
  std::size_t pos = 0;
  while ((pos = json.find("\"order_id\"", pos)) != std::string::npos) {
    BookRow row{};
    const auto p = json.find(':', pos);
    if (p == std::string::npos) break;
    std::size_t i = p + 1;
    while (i < json.size() && (json[i] < '0' || json[i] > '9')) ++i;
    std::uint64_t oid = 0;
    while (i < json.size() && json[i] >= '0' && json[i] <= '9') {
      oid = oid * 10 + static_cast<std::uint64_t>(json[i] - '0');
      ++i;
    }
    row.order_id = oid;
    const auto price_key = json.find("\"price\"", pos);
    const auto amt_key = json.find("\"amount\"", pos);
    if (price_key == std::string::npos || amt_key == std::string::npos) break;
    auto parse_num = [&](std::size_t key_pos, double& v) {
      const auto c = json.find(':', key_pos);
      if (c == std::string::npos) return false;
      std::size_t j = c + 1;
      while (j < json.size() && (json[j] == ' ' || json[j] == '\t')) ++j;
      const std::size_t start = j;
      while (j < json.size() && (json[j] == '-' || json[j] == '.' || (json[j] >= '0' && json[j] <= '9'))) ++j;
      try {
        v = std::stod(json.substr(start, j - start));
        return true;
      } catch (...) {
        return false;
      }
    };
    if (!parse_num(price_key, row.price) || !parse_num(amt_key, row.amount)) break;
    out.rows.push_back(row);
    pos = amt_key + 8;
  }
  return !out.rows.empty() || !out.symbol.empty();
}

}  // namespace mf::live::bitfinex
