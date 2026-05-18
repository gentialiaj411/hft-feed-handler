#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace mf::wire {

enum class WireProtocol : std::uint8_t {
  NasdaqItch50 = 0,
  IexDeep = 1,
  CboePitch = 2,
};

struct FrameSlice {
  const std::uint8_t* data{nullptr};
  std::size_t len{0};
  std::uint64_t sequence{0};
};

class DatagramFramer {
 public:
  explicit DatagramFramer(WireProtocol protocol) : protocol_(protocol) {}
  void frame(const std::uint8_t* datagram, std::size_t len, std::function<void(FrameSlice)> on_frame) const;

 private:
  WireProtocol protocol_{WireProtocol::NasdaqItch50};
};

}  // namespace mf::wire
