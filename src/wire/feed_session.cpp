#include "mf/wire/feed_session.hpp"

#include <cerrno>
#include <span>

namespace mf::wire {

FeedSession::FeedSession(
    McastReceiverConfig receiver_cfg,
    WireProtocol protocol,
    mf::phase2::FeedSide side,
    std::function<void(mf::phase2::FeedSide, const mf::core::BookEvent&)> sink_cb)
    : receiver_(std::move(receiver_cfg)), framer_(protocol), side_(side), sink_cb_(std::move(sink_cb)) {
  switch (protocol) {
    case WireProtocol::NasdaqItch50:
      parser_ = mf::proto::nasdaq::Itch50Parser{};
      parse_stats_ = mf::proto::nasdaq::ParseStats{};
      break;
    case WireProtocol::IexDeep:
      parser_ = mf::proto::iex::DeepParser{};
      parse_stats_ = mf::proto::iex::ParseStats{};
      break;
    case WireProtocol::CboePitch:
      parser_ = mf::proto::cboe::PitchParser{};
      parse_stats_ = mf::proto::cboe::ParseStats{};
      break;
  }
}

bool FeedSession::open() { return receiver_.open(); }

void FeedSession::poll() {
  for (;;) {
    std::uint64_t ingest_ts_ns = 0;
    const ssize_t n = receiver_.recv(recv_buf_.data(), recv_buf_.size(), ingest_ts_ns);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      break;
    }
    if (n == 0) break;
    ++stats_.datagrams_received;
    stats_.bytes_received += static_cast<std::uint64_t>(n);
    framer_.frame(recv_buf_.data(), static_cast<std::size_t>(n), [&](FrameSlice slice) {
      ++stats_.frames_parsed;
      auto ev = parse_frame(slice.data, slice.len, slice.sequence, ingest_ts_ns);
      if (!ev.has_value()) {
        ++stats_.parse_failures;
        return;
      }
      sink_cb_(side_, *ev);
      ++stats_.sink_callbacks_fired;
    });
  }
}

void FeedSession::close() { receiver_.close(); }

std::optional<mf::core::BookEvent> FeedSession::parse_frame(
    const std::uint8_t* data,
    std::size_t len,
    std::uint64_t seq,
    std::uint64_t ingest_ts_ns) {
  const auto payload = std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), len);
  if (auto* p = std::get_if<mf::proto::nasdaq::Itch50Parser>(&parser_)) {
    auto* s = std::get_if<mf::proto::nasdaq::ParseStats>(&parse_stats_);
    return p->parse_message(payload, seq, ingest_ts_ns, *s);
  }
  if (auto* p = std::get_if<mf::proto::iex::DeepParser>(&parser_)) {
    auto* s = std::get_if<mf::proto::iex::ParseStats>(&parse_stats_);
    return p->parse_message(payload, seq, ingest_ts_ns, *s);
  }
  auto* p = std::get_if<mf::proto::cboe::PitchParser>(&parser_);
  auto* s = std::get_if<mf::proto::cboe::ParseStats>(&parse_stats_);
  return p->parse_message(payload, seq, ingest_ts_ns, *s);
}

}  // namespace mf::wire
