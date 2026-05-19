#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "mf/core/time.hpp"
#include "mf/journal/journal_writer.hpp"
#include "mf/proto/nasdaq/itch50_parser.hpp"

namespace {

std::string arg(int argc, char** argv, const std::string& key, const std::string& dflt) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == key) return argv[i + 1];
  }
  return dflt;
}

std::uint64_t arg_u64(int argc, char** argv, const std::string& key, std::uint64_t dflt) {
  const std::string v = arg(argc, argv, key, "");
  if (v.empty()) return dflt;
  return static_cast<std::uint64_t>(std::stoull(v));
}

bool has_flag(int argc, char** argv, const std::string& key) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == key) return true;
  }
  return false;
}

std::uint16_t read_u16_be(const std::uint8_t* p) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8U) |
                                    static_cast<std::uint16_t>(p[1]));
}

void usage() {
  std::cerr << "usage: itch_to_journal --in <itch_path> --out <journal_path> [--max-events N] [--max-bytes N] [--append]\n";
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(__linux__)
  std::cout << "itch_to_journal is Linux-only\n";
  return 0;
#else
  const std::string in_path = arg(argc, argv, "--in", "");
  const std::string out_path = arg(argc, argv, "--out", "");
  const std::uint64_t max_events = arg_u64(argc, argv, "--max-events", 0);
  const std::uint64_t max_bytes = arg_u64(argc, argv, "--max-bytes", 0);
  const bool append = has_flag(argc, argv, "--append");
  if (in_path.empty() || out_path.empty()) {
    usage();
    return 2;
  }

  std::ifstream in(in_path, std::ios::binary);
  if (!in) {
    std::cerr << "failed to open input: " << in_path << "\n";
    return 1;
  }

  mf::journal::JournalWriter writer;
  if (!append && std::filesystem::exists(out_path)) {
    std::error_code ec;
    std::filesystem::remove(out_path, ec);
    if (ec) {
      std::cerr << "failed to remove existing output journal: " << out_path << ": " << ec.message() << "\n";
      return 1;
    }
  }
  if (!writer.open(out_path)) {
    std::cerr << "failed to open output journal: " << out_path << "\n";
    return 1;
  }

  mf::proto::nasdaq::Itch50Parser parser;
  mf::proto::nasdaq::ParseStats stats{};

  std::uint64_t bytes_read = 0;
  std::uint64_t messages_parsed = 0;
  std::uint64_t events_written = 0;
  std::uint64_t unhandled_msg_count = 0;
  std::uint64_t last_exchange_ts_ns = 0;

  const auto t0 = std::chrono::steady_clock::now();
  std::array<std::uint8_t, 2> len_buf{};
  std::uint64_t seq = 1;

  while (in.read(reinterpret_cast<char*>(len_buf.data()), len_buf.size())) {
    bytes_read += 2;
    if (max_bytes > 0 && bytes_read > max_bytes) {
      break;
    }

    const std::uint16_t msg_len = read_u16_be(len_buf.data());
    if (msg_len == 0) {
      ++unhandled_msg_count;
      continue;
    }
    if (max_bytes > 0 && bytes_read + msg_len > max_bytes) {
      break;
    }

    std::vector<std::byte> msg(msg_len);
    if (!in.read(reinterpret_cast<char*>(msg.data()), static_cast<std::streamsize>(msg_len))) {
      break;
    }
    bytes_read += msg_len;
    ++messages_parsed;

    auto ev = parser.parse_message(
        std::span<const std::byte>(msg.data(), msg.size()),
        seq++,
        mf::core::monotonic_raw_now_ns(),
        stats);
    if (!ev.has_value()) {
      continue;
    }

    if (ev->type == mf::core::EventType::Unknown) {
      ++unhandled_msg_count;
    }
    if (ev->exchange_ts_ns == 0 && last_exchange_ts_ns != 0) {
      ev->exchange_ts_ns = last_exchange_ts_ns;
    } else if (ev->exchange_ts_ns != 0) {
      last_exchange_ts_ns = ev->exchange_ts_ns;
    }

    writer.append(*ev, ev->ingest_ts_ns);
    ++events_written;
    if (max_events > 0 && events_written >= max_events) {
      break;
    }
  }

  writer.close();
  const auto t1 = std::chrono::steady_clock::now();
  const double wall_time_sec = std::chrono::duration<double>(t1 - t0).count();

  std::cout << "bytes_read=" << bytes_read << "\n";
  std::cout << "messages_parsed=" << messages_parsed << "\n";
  std::cout << "events_written=" << events_written << "\n";
  std::cout << "unhandled_msg_count=" << unhandled_msg_count << "\n";
  std::cout << "parser_malformed_messages=" << stats.malformed_messages << "\n";
  std::cout << "wall_time_sec=" << wall_time_sec << "\n";
  return 0;
#endif
}
