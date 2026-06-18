#include "mf/live/bitfinex/wire_types.hpp"

#include <charconv>
#include <cctype>
#include <cmath>
#include <string_view>

namespace mf::live::bitfinex {

namespace {

void skip_ws(std::string_view s, std::size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
}

bool parse_i64(std::string_view s, std::size_t& i, std::int64_t& out) {
  skip_ws(s, i);
  if (i >= s.size()) return false;
  const std::size_t start = i;
  if (s[i] == '-') ++i;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
  if (i == start || (i == start + 1 && s[start] == '-')) return false;
  std::int64_t v = 0;
  const auto sub = s.substr(start, i - start);
  const auto r = std::from_chars(sub.data(), sub.data() + sub.size(), v);
  if (r.ec != std::errc{}) return false;
  out = v;
  return true;
}

bool parse_u64(std::string_view s, std::size_t& i, std::uint64_t& out) {
  std::int64_t v = 0;
  if (!parse_i64(s, i, v) || v < 0) return false;
  out = static_cast<std::uint64_t>(v);
  return true;
}

bool parse_double(std::string_view s, std::size_t& i, double& out) {
  skip_ws(s, i);
  if (i >= s.size()) return false;
  const std::size_t start = i;
  if (s[i] == '-') ++i;
  while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
  if (i == start || (i == start + 1 && s[start] == '-')) return false;
  const auto sub = s.substr(start, i - start);
  double v = 0.0;
  const auto r = std::from_chars(sub.data(), sub.data() + sub.size(), v);
  if (r.ec != std::errc{}) return false;
  out = v;
  return true;
}

bool expect(std::string_view s, std::size_t& i, char c) {
  skip_ws(s, i);
  if (i >= s.size() || s[i] != c) return false;
  ++i;
  return true;
}

bool parse_string_token(std::string_view s, std::size_t& i, std::string& out) {
  skip_ws(s, i);
  if (i >= s.size() || s[i] != '"') return false;
  ++i;
  const std::size_t start = i;
  while (i < s.size() && s[i] != '"') ++i;
  if (i >= s.size()) return false;
  out.assign(s.substr(start, i - start));
  ++i;
  return true;
}

bool parse_row(std::string_view s, std::size_t& i, BookRow& row) {
  if (!expect(s, i, '[')) return false;
  if (!parse_u64(s, i, row.order_id)) return false;
  if (!expect(s, i, ',')) return false;
  if (!parse_double(s, i, row.price)) return false;
  if (!expect(s, i, ',')) return false;
  if (!parse_double(s, i, row.amount)) return false;
  if (!expect(s, i, ']')) return false;
  return true;
}

bool parse_snapshot_rows(std::string_view s, std::size_t& i, std::vector<BookRow>& rows) {
  if (!expect(s, i, '[')) return false;
  skip_ws(s, i);
  if (i < s.size() && s[i] == ']') {
    ++i;
    return true;
  }
  for (;;) {
    BookRow row{};
    if (!parse_row(s, i, row)) return false;
    rows.push_back(row);
    skip_ws(s, i);
    if (i < s.size() && s[i] == ']') {
      ++i;
      return true;
    }
    if (!expect(s, i, ',')) return false;
  }
}

}  // namespace

bool parse_frame(const std::string& json, ParsedFrame& out) noexcept {
  out = {};
  std::string_view s{json};
  std::size_t i = 0;
  if (!expect(s, i, '[')) {
    if (expect(s, i, '{')) {
      out.kind = ParsedKind::Control;
      std::string key;
      while (i < s.size()) {
        if (!parse_string_token(s, i, key)) break;
        if (!expect(s, i, ':')) break;
        if (key == "event") {
          parse_string_token(s, i, out.control_event);
        } else {
          skip_ws(s, i);
          if (i < s.size() && s[i] == '"') {
            std::string tmp;
            parse_string_token(s, i, tmp);
          } else {
            double d = 0;
            parse_double(s, i, d);
          }
        }
        skip_ws(s, i);
        if (i < s.size() && s[i] == ',') {
          ++i;
          continue;
        }
        break;
      }
      return !out.control_event.empty();
    }
    return false;
  }

  std::int64_t chan = 0;
  if (!parse_i64(s, i, chan)) return false;
  if (!expect(s, i, ',')) return false;

  skip_ws(s, i);
  if (i < s.size() && s[i] == '"') {
    std::string tag;
    if (!parse_string_token(s, i, tag)) return false;
    if (tag == "hb") {
      out.kind = ParsedKind::Heartbeat;
      return true;
    }
    return false;
  }

  if (i < s.size() && s[i] == '[') {
    const std::size_t peek = i;
    std::size_t j = peek + 1;
    skip_ws(s, j);
    if (j < s.size() && s[j] == '[') {
      out.kind = ParsedKind::Snapshot;
      out.snapshot.channel_id = chan;
      i = peek;
      if (!parse_snapshot_rows(s, i, out.snapshot.rows)) return false;
      skip_ws(s, i);
      if (i < s.size() && s[i] == ',') {
        ++i;
        (void)parse_u64(s, i, out.snapshot.sequence);
      }
      skip_ws(s, i);
      return i < s.size() && s[i] == ']';
    }
    out.kind = ParsedKind::Update;
    out.update.channel_id = chan;
    i = peek;
    if (!parse_row(s, i, out.update.row)) return false;
    skip_ws(s, i);
    if (i < s.size() && s[i] == ',') {
      ++i;
      return parse_u64(s, i, out.update.sequence);
    }
    return true;
  }

  return false;
}

}  // namespace mf::live::bitfinex
