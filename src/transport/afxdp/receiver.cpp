#include "mf/transport/afxdp/receiver.hpp"

#if defined(__linux__) && defined(MF_HAS_LIBBPF)
#include <linux/if_xdp.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <net/if.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "mf/core/time.hpp"
#include "mf/transport/afxdp/udp_payload.hpp"

namespace {

static inline __u64* map_u64(void* base, __u64 offset) {
  return reinterpret_cast<__u64*>(static_cast<char*>(base) + offset);
}

static inline struct xdp_desc* map_desc(void* base, __u64 offset, __u32 idx) {
  return reinterpret_cast<struct xdp_desc*>(static_cast<char*>(base) + offset) + idx;
}

static inline __u64* map_fill(void* base, __u64 offset, __u32 idx) {
  return reinterpret_cast<__u64*>(static_cast<char*>(base) + offset) + idx;
}

}  // namespace

namespace mf::transport::afxdp {

struct AfxdpReceiver::Impl {
  AfxdpConfig cfg{};
  bool available{true};
  int xsk_fd{-1};
  void* umem_area{nullptr};
  std::uint64_t umem_size{0};
  std::uint32_t frame_size{0};
  std::uint32_t frame_count{0};
  std::uint64_t fill_prod{0};
  std::uint64_t rx_prod{0};
  std::uint64_t rx_cons{0};
  struct xdp_ring_offset fill_off {};
  struct xdp_ring_offset rx_off {};
  void* fill_ring{nullptr};
  void* rx_ring{nullptr};
  AfxdpReceiverStats stats{};
};

AfxdpReceiver::AfxdpReceiver(AfxdpConfig cfg) : impl_(new Impl{std::move(cfg), true, -1, nullptr, 0, 0, 0, 0, 0, 0, {}, {}, nullptr, nullptr, {}}) {}
AfxdpReceiver::~AfxdpReceiver() {
  close();
  delete impl_;
  impl_ = nullptr;
}

bool AfxdpReceiver::available() const noexcept { return impl_ != nullptr && impl_->available; }

bool AfxdpReceiver::open() {
  close();
  if (impl_ == nullptr) {
    return false;
  }
  if (impl_->cfg.ifindex == 0 && !impl_->cfg.ifname.empty()) {
    impl_->cfg.ifindex = static_cast<int>(::if_nametoindex(impl_->cfg.ifname.c_str()));
  }
  if (impl_->cfg.ifindex <= 0) {
    return false;
  }

  impl_->frame_size = impl_->cfg.frame_size;
  impl_->frame_count = impl_->cfg.frame_count;
  impl_->umem_size = static_cast<std::uint64_t>(impl_->frame_size) * static_cast<std::uint64_t>(impl_->frame_count);
  impl_->umem_area = ::mmap(nullptr, static_cast<std::size_t>(impl_->umem_size), PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
  if (impl_->umem_area == MAP_FAILED) {
    impl_->umem_area = ::mmap(nullptr, static_cast<std::size_t>(impl_->umem_size), PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  }
  if (impl_->umem_area == MAP_FAILED) {
    impl_->umem_area = nullptr;
    return false;
  }

  impl_->xsk_fd = ::socket(AF_XDP, SOCK_RAW, 0);
  if (impl_->xsk_fd < 0) {
    close();
    return false;
  }

  struct xdp_umem_reg mr {};
  mr.addr = reinterpret_cast<std::uint64_t>(impl_->umem_area);
  mr.len = impl_->umem_size;
  mr.chunk_size = impl_->frame_size;
  if (::setsockopt(impl_->xsk_fd, SOL_XDP, XDP_UMEM_REG, &mr, sizeof(mr)) != 0) {
    close();
    return false;
  }

  for (const int ring : {XDP_UMEM_FILL_RING, XDP_UMEM_COMPLETION_RING, XDP_RX_RING, XDP_TX_RING}) {
    int rc = 0;
    socklen_t optlen = sizeof(rc);
    if (::getsockopt(impl_->xsk_fd, SOL_XDP, ring, &rc, &optlen) != 0 || rc <= 0) {
      close();
      return false;
    }
  }

  socklen_t optlen = sizeof(impl_->fill_off);
  if (::getsockopt(impl_->xsk_fd, SOL_XDP, XDP_UMEM_FILL_RING, &impl_->fill_off, &optlen) != 0) {
    close();
    return false;
  }
  optlen = sizeof(impl_->rx_off);
  if (::getsockopt(impl_->xsk_fd, SOL_XDP, XDP_RX_RING, &impl_->rx_off, &optlen) != 0) {
    close();
    return false;
  }

  impl_->fill_ring = ::mmap(nullptr, impl_->fill_off.desc, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
      impl_->xsk_fd, XDP_UMEM_FILL_RING);
  impl_->rx_ring = ::mmap(nullptr, impl_->rx_off.desc, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
      impl_->xsk_fd, XDP_RX_RING);
  if (impl_->fill_ring == MAP_FAILED || impl_->rx_ring == MAP_FAILED) {
    close();
    return false;
  }

  impl_->fill_prod = *map_u64(impl_->fill_ring, impl_->fill_off.producer);
  impl_->rx_prod = *map_u64(impl_->rx_ring, impl_->rx_off.producer);
  impl_->rx_cons = *map_u64(impl_->rx_ring, impl_->rx_off.consumer);

  const std::uint32_t reserve = impl_->frame_count / 2U;
  for (std::uint32_t i = 0; i < reserve; ++i) {
    const std::uint64_t idx = impl_->fill_prod++ & (impl_->frame_count - 1U);
    *map_fill(impl_->fill_ring, impl_->fill_off.desc, static_cast<__u32>(idx)) =
        static_cast<__u64>(i * impl_->frame_size);
  }
  *map_u64(impl_->fill_ring, impl_->fill_off.producer) = impl_->fill_prod;

  sockaddr_xdp sxdp {};
  sxdp.sxdp_family = AF_XDP;
  sxdp.sxdp_ifindex = static_cast<unsigned int>(impl_->cfg.ifindex);
  sxdp.sxdp_queue_id = impl_->cfg.queue_id;
  if (impl_->cfg.bind_flags_copy) {
    sxdp.sxdp_flags = XDP_COPY;
  }
  if (::bind(impl_->xsk_fd, reinterpret_cast<sockaddr*>(&sxdp), sizeof(sxdp)) != 0) {
    close();
    return false;
  }

  if (!impl_->cfg.bpf_obj_path.empty()) {
    if (!attach_xdp_redirect(impl_->cfg, impl_->xsk_fd)) {
      close();
      return false;
    }
  }
  return true;
}

ssize_t AfxdpReceiver::recv(std::uint8_t* buf, const std::size_t buflen, std::uint64_t& ingest_ts_ns) {
  ingest_ts_ns = mf::core::monotonic_raw_now_ns();
  if (impl_ == nullptr || impl_->xsk_fd < 0 || buf == nullptr || buflen == 0) {
    return -1;
  }

  impl_->rx_prod = *map_u64(impl_->rx_ring, impl_->rx_off.producer);
  if (impl_->rx_cons == impl_->rx_prod) {
    return -1;
  }

  const std::uint32_t idx = static_cast<std::uint32_t>(impl_->rx_cons & (impl_->frame_count - 1U));
  const struct xdp_desc desc = *map_desc(impl_->rx_ring, impl_->rx_off.desc, idx);
  ++impl_->rx_cons;
  *map_u64(impl_->rx_ring, impl_->rx_off.consumer) = impl_->rx_cons;

  const auto* frame = reinterpret_cast<const std::uint8_t*>(static_cast<const char*>(impl_->umem_area) + desc.addr);
  ++impl_->stats.frames_received;
  impl_->stats.bytes_received += desc.len;

  const std::uint8_t* payload = nullptr;
  const std::size_t payload_len = extract_udp_payload(frame, desc.len, payload);
  if (payload_len == 0 || payload == nullptr) {
    ++impl_->stats.parse_skips;
    const std::uint64_t fill_idx = impl_->fill_prod & (impl_->frame_count - 1U);
    *map_fill(impl_->fill_ring, impl_->fill_off.desc, static_cast<__u32>(fill_idx)) = desc.addr;
    ++impl_->fill_prod;
    *map_u64(impl_->fill_ring, impl_->fill_off.producer) = impl_->fill_prod;
    return -1;
  }

  ++impl_->stats.udp_payloads;
  const std::size_t copy_len = std::min(buflen, payload_len);
  std::memcpy(buf, payload, copy_len);

  const std::uint64_t fill_idx = impl_->fill_prod & (impl_->frame_count - 1U);
  *map_fill(impl_->fill_ring, impl_->fill_off.desc, static_cast<__u32>(fill_idx)) = desc.addr;
  ++impl_->fill_prod;
  *map_u64(impl_->fill_ring, impl_->fill_off.producer) = impl_->fill_prod;

  ingest_ts_ns = mf::core::monotonic_raw_now_ns();
  return static_cast<ssize_t>(copy_len);
}

void AfxdpReceiver::close() {
  if (impl_ == nullptr) {
    return;
  }
  if (impl_->fill_ring != nullptr && impl_->fill_ring != MAP_FAILED) {
    (void)::munmap(impl_->fill_ring, impl_->fill_off.desc);
  }
  if (impl_->rx_ring != nullptr && impl_->rx_ring != MAP_FAILED) {
    (void)::munmap(impl_->rx_ring, impl_->rx_off.desc);
  }
  if (impl_->umem_area != nullptr && impl_->umem_area != MAP_FAILED) {
    (void)::munmap(impl_->umem_area, static_cast<std::size_t>(impl_->umem_size));
  }
  if (impl_->xsk_fd >= 0) {
    ::close(impl_->xsk_fd);
  }
  impl_->fill_ring = nullptr;
  impl_->rx_ring = nullptr;
  impl_->umem_area = nullptr;
  impl_->xsk_fd = -1;
}

const AfxdpReceiverStats& AfxdpReceiver::stats() const noexcept {
  static const AfxdpReceiverStats kEmpty{};
  return impl_ != nullptr ? impl_->stats : kEmpty;
}

bool attach_xdp_redirect(AfxdpConfig& cfg, const int xsk_fd) {
  if (cfg.bpf_obj_path.empty() || cfg.ifindex <= 0) {
    return false;
  }

  struct bpf_object* obj = bpf_object__open(cfg.bpf_obj_path.c_str());
  if (obj == nullptr || libbpf_get_error(obj)) {
    if (obj != nullptr) {
      bpf_object__close(obj);
    }
    return false;
  }
  if (bpf_object__load(obj) != 0) {
    bpf_object__close(obj);
    return false;
  }

  struct bpf_program* prog = bpf_object__find_program_by_name(obj, "afxdp_redirect");
  if (prog == nullptr) {
    prog = bpf_object__find_program_by_name(obj, cfg.bpf_sec.c_str());
  }
  if (prog == nullptr) {
    bpf_object__close(obj);
    return false;
  }

  struct bpf_link* link = bpf_program__attach_xdp(prog, cfg.ifindex);
  if (link == nullptr) {
    bpf_object__close(obj);
    return false;
  }

  struct bpf_map* map = bpf_object__find_map_by_name(obj, "xsks_map");
  if (map == nullptr) {
    bpf_link__destroy(link);
    bpf_object__close(obj);
    return false;
  }
  const int map_fd = bpf_map__fd(map);
  const __u32 qid = cfg.queue_id;
  if (bpf_map_update_elem(map_fd, &qid, &xsk_fd, 0) != 0) {
    bpf_link__destroy(link);
    bpf_object__close(obj);
    return false;
  }

  (void)link;
  (void)obj;
  return true;
}

}  // namespace mf::transport::afxdp

#else

namespace mf::transport::afxdp {

struct AfxdpReceiver::Impl {};

AfxdpReceiver::AfxdpReceiver(AfxdpConfig) : impl_(new Impl{}) {}
AfxdpReceiver::~AfxdpReceiver() { delete impl_; }
bool AfxdpReceiver::available() const noexcept { return false; }
bool AfxdpReceiver::open() { return false; }
ssize_t AfxdpReceiver::recv(std::uint8_t*, std::size_t, std::uint64_t&) { return -1; }
void AfxdpReceiver::close() {}
const AfxdpReceiverStats& AfxdpReceiver::stats() const noexcept {
  static const AfxdpReceiverStats kEmpty{};
  return kEmpty;
}
bool attach_xdp_redirect(AfxdpConfig&, int) { return false; }

}  // namespace mf::transport::afxdp

#endif
