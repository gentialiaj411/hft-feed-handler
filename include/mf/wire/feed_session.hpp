#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <variant>

#include "mf/core/types.hpp"
#include "mf/phase2/ab_arbiter.hpp"
#include "mf/proto/cboe/pitch_parser.hpp"
#include "mf/proto/iex/deep_parser.hpp"
#include "mf/proto/nasdaq/itch50_parser.hpp"
#include "mf/wire/framing.hpp"
#include "mf/wire/udp_multicast_receiver.hpp"

namespace mf::wire {

// Steady-state recv -> frame -> parse -> callback path is allocation-free:
// datagram storage is a fixed stack/inline buffer and parser/framer work on slices.
struct FeedSessionStats {
  std::uint64_t datagrams_received{0};
  std::uint64_t bytes_received{0};
  std::uint64_t frames_parsed{0};
  std::uint64_t parse_failures{0};
  std::uint64_t sink_callbacks_fired{0};
};

class FeedSession {
 public:
  FeedSession(
      McastReceiverConfig receiver_cfg,
      WireProtocol protocol,
      mf::phase2::FeedSide side,
      std::function<void(mf::phase2::FeedSide, const mf::core::BookEvent&)> sink_cb);

  bool open();
  void poll();
  void close();
  [[nodiscard]] int fd() const { return receiver_.fd(); }
  [[nodiscard]] const FeedSessionStats& stats() const noexcept { return stats_; }
  [[nodiscard]] mf::phase2::FeedSide side() const noexcept { return side_; }

 private:
  using ParserVariant = std::variant<mf::proto::nasdaq::Itch50Parser, mf::proto::iex::DeepParser, mf::proto::cboe::PitchParser>;
  using ParseStatsVariant = std::variant<mf::proto::nasdaq::ParseStats, mf::proto::iex::ParseStats, mf::proto::cboe::ParseStats>;

  std::optional<mf::core::BookEvent> parse_frame(const std::uint8_t* data, std::size_t len, std::uint64_t seq, std::uint64_t ingest_ts_ns);

  UdpMulticastReceiver receiver_;
  DatagramFramer framer_;
  mf::phase2::FeedSide side_{mf::phase2::FeedSide::A};
  ParserVariant parser_{mf::proto::nasdaq::Itch50Parser{}};
  ParseStatsVariant parse_stats_{mf::proto::nasdaq::ParseStats{}};
  std::function<void(mf::phase2::FeedSide, const mf::core::BookEvent&)> sink_cb_;
  std::array<std::uint8_t, 64 * 1024> recv_buf_{};
  FeedSessionStats stats_{};
};

}  // namespace mf::wire
