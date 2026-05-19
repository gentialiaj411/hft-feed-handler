#pragma once

#include <cstddef>
#include <cstdint>

#include "mf/core/spmc_seqlock_ring.hpp"
#include "mf/core/spsc_ring.hpp"
#include "mf/phase2/pipeline.hpp"
#include "mf/phase3/feature_pipeline.hpp"
#include "mf/phase3/nbbo_consolidator.hpp"
#include "mf/phase3/order_book_engine.hpp"
#include "mf/phase3/types.hpp"

namespace mf::phase3 {

class IFeaturePublisher {
 public:
  virtual ~IFeaturePublisher() = default;
  virtual bool try_publish(const FeatureVector& fv) noexcept = 0;
};

struct FeatureBridgeStats {
  std::uint64_t published{0};
  std::uint64_t dropped{0};
  std::uint64_t feature_updates{0};
};

class FeatureBridge final : public mf::phase2::IMergedEventSink {
 public:
  explicit FeatureBridge(IFeaturePublisher* publisher)
      : publisher_(publisher), features_() {}

  explicit FeatureBridge(IFeaturePublisher* publisher, FeaturePipeline::Config cfg)
      : publisher_(publisher), features_(cfg) {}

  void on_merged_event(const mf::core::BookEvent& ev) noexcept override;
  [[nodiscard]] const FeatureBridgeStats& stats() const noexcept { return stats_; }

 private:
  IFeaturePublisher* publisher_{nullptr};
  OrderBookEngine books_{};
  NbboConsolidator nbbo_{};
  FeaturePipeline features_{};
  FeatureBridgeStats stats_{};
};

template <std::size_t Capacity>
class RingFeaturePublisher final : public IFeaturePublisher {
 public:
  explicit RingFeaturePublisher(mf::core::SPSCRingBuffer<FeatureVector, Capacity>* ring)
      : ring_(ring) {}

  bool try_publish(const FeatureVector& fv) noexcept override {
    if (ring_ == nullptr) {
      return false;
    }
    return ring_->try_push(fv);
  }

 private:
  mf::core::SPSCRingBuffer<FeatureVector, Capacity>* ring_{nullptr};
};

template <std::size_t Capacity>
class SpmcFeaturePublisher final : public IFeaturePublisher {
 public:
  explicit SpmcFeaturePublisher(mf::core::SPMCSeqlockRing<FeatureVector, Capacity>* ring)
      : ring_(ring) {}

  bool try_publish(const FeatureVector& fv) noexcept override {
    if (ring_ == nullptr) {
      return false;
    }
    return ring_->try_publish(fv);
  }

 private:
  mf::core::SPMCSeqlockRing<FeatureVector, Capacity>* ring_{nullptr};
};

}  // namespace mf::phase3
