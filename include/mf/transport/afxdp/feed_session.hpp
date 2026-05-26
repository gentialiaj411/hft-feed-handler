#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "mf/core/types.hpp"
#include "mf/proto/nasdaq/itch50_parser.hpp"
#include "mf/transport/afxdp/config.hpp"
#include "mf/transport/afxdp/receiver.hpp"
#include "mf/wire/framing.hpp"

namespace mf::transport::afxdp {

struct AfxdpFeedSessionStats {
  std::uint64_t datagrams_received{0};
  std::uint64_t bytes_received{0};
  std::uint64_t frames_parsed{0};
  std::uint64_t parse_failures{0};
  std::uint64_t sink_callbacks_fired{0};
};

class AfxdpFeedSession {
 public:
  using SinkFn = std::function<void(const mf::core::BookEvent&)>;

  AfxdpFeedSession(AfxdpConfig cfg, SinkFn sink_cb);

  [[nodiscard]] bool open();
  void poll();
  void close();
  [[nodiscard]] const AfxdpFeedSessionStats& stats() const noexcept { return stats_; }
  [[nodiscard]] const AfxdpReceiverStats& receiver_stats() const noexcept { return receiver_.stats(); }

 private:
  AfxdpReceiver receiver_;
  mf::wire::DatagramFramer framer_;
  mf::proto::nasdaq::Itch50Parser parser_{};
  mf::proto::nasdaq::ParseStats parse_stats_{};
  SinkFn sink_cb_;
  std::array<std::uint8_t, 64 * 1024> recv_buf_{};
  AfxdpFeedSessionStats stats_{};
};

}  // namespace mf::transport::afxdp
