#pragma once

#include <cstddef>
#include <cstdint>

#include "mf/core/spsc_ring.hpp"
#include "mf/core/types.hpp"
#include "mf/phase2/pipeline.hpp"

namespace mf::phase2 {

struct JitSignalEvent {
  std::uint8_t venue{0};
  std::uint8_t type{0};
  std::uint8_t side{0};
  std::uint8_t raw_type{0};
  std::uint64_t sequence{0};
  std::uint64_t exchange_ts_ns{0};
  std::uint64_t ingest_ts_ns{0};
  std::uint64_t symbol_u64{0};
  std::uint64_t order_id{0};
  std::uint64_t match_id{0};
  std::uint32_t qty{0};
  std::uint32_t price{0};
};
static_assert(sizeof(JitSignalEvent) == 64, "JitSignalEvent layout changed; update JIT schema version");
static_assert(offsetof(JitSignalEvent, sequence) == 8, "JitSignalEvent field offset drift");

class IJitPublisher {
 public:
  virtual ~IJitPublisher() = default;
  virtual bool try_publish(const JitSignalEvent& ev) noexcept = 0;
};

struct JitBridgeStats {
  std::uint64_t published{0};
  std::uint64_t dropped{0};
};

class JitBridge final : public IMergedEventSink {
 public:
  explicit JitBridge(IJitPublisher* publisher) : publisher_(publisher) {}

  void on_merged_event(const mf::core::BookEvent& ev) noexcept override;
  [[nodiscard]] const JitBridgeStats& stats() const noexcept { return stats_; }

 private:
  static JitSignalEvent convert(const mf::core::BookEvent& ev) noexcept;

  IJitPublisher* publisher_{nullptr};
  JitBridgeStats stats_{};
};

template <std::size_t Capacity>
class RingJitPublisher final : public IJitPublisher {
 public:
  explicit RingJitPublisher(mf::core::SPSCRingBuffer<JitSignalEvent, Capacity>* ring)
      : ring_(ring) {}

  bool try_publish(const JitSignalEvent& ev) noexcept override {
    if (ring_ == nullptr) {
      return false;
    }
    return ring_->try_push(ev);
  }

 private:
  mf::core::SPSCRingBuffer<JitSignalEvent, Capacity>* ring_{nullptr};
};

}  // namespace mf::phase2
