#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <span>
#include <vector>

#include "mf/core/book_event_crc.hpp"
#include "mf/core/types.hpp"
#include "mf/journal/journal_semantic_crc.hpp"
#include "mf/journal/journal_writer.hpp"
#include "mf/proto/mdp3/cert_bin_reader.hpp"
#include "mf/proto/mdp3/mdp3_parser.hpp"
#include "mf/proto/mdp3/sbe_header.hpp"

namespace {

std::vector<std::byte> read_file_or_empty(const char* path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> out(raw.size());
  std::memcpy(out.data(), raw.data(), raw.size());
  return out;
}

void test_sbe_header() {
  // Header declares msg_size = 44 (0x2c). decode_sbe_header rejects messages
  // whose declared size exceeds the provided span, so we have to provide a
  // 44-byte buffer (10-byte header + 34 bytes of body padding) for the decode
  // to succeed.
  std::uint8_t raw[44] = {0x2c, 0x00, 0x0b, 0x00, 0x20, 0x00, 0x01, 0x00, 0x09, 0x00};
  mf::proto::mdp3::SbeMessageHeader hdr {};
  if (!mf::proto::mdp3::decode_sbe_header(
          std::span<const std::byte>(reinterpret_cast<const std::byte*>(raw), sizeof(raw)),
          hdr)) {
    std::fprintf(stderr, "decode_sbe_header rejected canonical fixture\n");
    std::abort();
  }
  assert(hdr.msg_size == 44);
  assert(hdr.template_id == 32);
  assert(hdr.schema_id == 1);
  assert(hdr.version == 9);
}

void test_cert_bin_first_packet() {
  auto data = read_file_or_empty("bench/data/mdp3_cert_incr_311_AX_17511.bin");
  if (data.empty()) {
    std::printf("SKIP: MDP3 sample missing (run scripts/extract_mdp3_sample.py)\n");
    return;
  }
  mf::proto::mdp3::CertBinReader reader(std::span<const std::byte>(data.data(), data.size()));
  std::span<const std::byte> udp {};
  assert(reader.next(udp));
  assert(udp.size() >= 12);
}

void test_template32_dispatch_and_journal() {
  auto data = read_file_or_empty("bench/data/mdp3_cert_incr_311_AX_17511.bin");
  if (data.empty()) {
    return;
  }
  mf::proto::mdp3::Mdp3Parser parser;
  mf::proto::mdp3::ParseStats stats {};
  mf::proto::mdp3::CertBinReader reader(std::span<const std::byte>(data.data(), data.size()));
  std::span<const std::byte> udp {};
  std::uint64_t packets = 0;
  std::vector<mf::core::BookEvent> events;
  while (reader.next(udp) && packets < 200) {
    ++packets;
    auto parsed = parser.parse_packet(udp, 1000 + packets, stats);
    events.insert(events.end(), parsed.begin(), parsed.end());
  }
  assert(stats.parsed_messages > 0);
  assert(stats.book_events_emitted > 0);
  assert(stats.template_counts[32] > 0);

  const char* journal = "/tmp/mf_mdp3_test.journal";
  mf::journal::JournalWriter writer;
  assert(writer.open(journal));
  std::uint32_t crc = 0;
  for (const auto& ev : events) {
    mf::core::update_crc_from_book_event(crc, ev);
    writer.append(ev, ev.ingest_ts_ns);
  }
  writer.close();
  mf::journal::JournalSemanticStats jstats {};
  assert(mf::journal::compute_semantic_crc(journal, jstats));
  assert(jstats.records == events.size());
  assert(jstats.crc == crc);
  std::printf("PASS mdp3_parser packets=%llu events=%llu crc=0x%08x\n",
      static_cast<unsigned long long>(packets),
      static_cast<unsigned long long>(events.size()),
      jstats.crc);
}

}  // namespace

int main() {
  test_sbe_header();
  test_cert_bin_first_packet();
  test_template32_dispatch_and_journal();
  return 0;
}
