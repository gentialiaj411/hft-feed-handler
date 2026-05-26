#pragma once

#include <cstdint>
#include <string>

namespace mf::transport::afxdp {

struct AfxdpConfig {
  std::string ifname{"veth1"};
  int ifindex{0};
  std::uint32_t queue_id{0};
  std::uint32_t frame_size{4096};
  std::uint32_t frame_count{4096};
  std::string bpf_obj_path{};
  std::string bpf_sec{"xdp"};
  bool bind_flags_copy{true};
};

}  // namespace mf::transport::afxdp
