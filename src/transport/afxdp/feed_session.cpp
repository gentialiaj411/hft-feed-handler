#include "mf/transport/afxdp/feed_session.hpp"

#if defined(__linux__) && defined(MF_HAS_LIBBPF)
#include <span>

#include "mf/core/time.hpp"
#include "mf/transport/afxdp/udp_payload.hpp"

namespace mf::transport::afxdp {

AfxdpFeedSession::AfxdpFeedSession(AfxdpConfig cfg, SinkFn sink_cb)
    : receiver_(std::move(cfg)), framer_(mf::wire::WireProtocol::NasdaqItch50), sink_cb_(std::move(sink_cb)) {}

bool AfxdpFeedSession::open() { return receiver_.open(); }

void AfxdpFeedSession::poll() {
  for (;;) {
    std::uint64_t ingest_ts_ns = 0;
    const ssize_t n = receiver_.recv(recv_buf_.data(), recv_buf_.size(), ingest_ts_ns);
    if (n < 0) {
      break;
    }
  if (n == 0) {
      break;
    }
    ++stats_.datagrams_received;
    stats_.bytes_received += static_cast<std::uint64_t>(n);
    framer_.frame(recv_buf_.data(), static_cast<std::size_t>(n), [&](mf::wire::FrameSlice slice) {
      ++stats_.frames_parsed;
      const auto payload = std::span<const std::byte>(reinterpret_cast<const std::byte*>(slice.data), slice.len);
      auto ev = parser_.parse_message(payload, slice.sequence, ingest_ts_ns, parse_stats_);
      if (!ev.has_value()) {
        ++stats_.parse_failures;
        return;
      }
      sink_cb_(*ev);
      ++stats_.sink_callbacks_fired;
    });
  }
}

void AfxdpFeedSession::close() { receiver_.close(); }

}  // namespace mf::transport::afxdp

#else

namespace mf::transport::afxdp {

AfxdpFeedSession::AfxdpFeedSession(AfxdpConfig, SinkFn) {}
bool AfxdpFeedSession::open() { return false; }
void AfxdpFeedSession::poll() {}
void AfxdpFeedSession::close() {}

}  // namespace mf::transport::afxdp

#endif
