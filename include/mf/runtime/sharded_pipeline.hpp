#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "mf/core/time.hpp"
#include "mf/core/types.hpp"
#include "mf/core/book_event_crc.hpp"
#include "mf/core/spsc_ring.hpp"
#include "mf/phase2/deterministic_merger.hpp"
#include "mf/phase2/pipeline.hpp"
#include "mf/phase3/feature_bridge.hpp"
#include "mf/runtime/shard_router.hpp"

#if defined(__linux__)
#include <sched.h>
#endif

namespace mf::runtime {

struct ShardedPipelineConfig {
  std::size_t shard_count{1};
  std::uint64_t gap_window{256};
  std::size_t per_venue_capacity{4096};
};

struct ShardedPipelineStats {
  std::uint64_t submitted_events{0};
  std::uint64_t routed_events{0};
  std::size_t shard_ring_capacity{0};
  std::vector<std::uint64_t> per_shard_events{};
  std::vector<std::uint64_t> per_shard_push_retries{};
  std::vector<std::uint64_t> per_shard_worker_wall_ns{};
  std::vector<int> per_shard_start_cpu{};
  std::vector<std::uintptr_t> per_shard_ring_address_mod64{};
  std::vector<std::uint64_t> publish_latency_ns{};
  std::uint64_t reaggregator_push_ns{0};
  std::uint64_t reaggregator_drain_ns{0};
  bool reaggregator_sorted_input_fast_path{false};
  std::uint32_t reaggregated_crc{0};
};

template <std::size_t ShardCapacity = (1U << 14U)>
class ShardedPipeline {
 public:
  explicit ShardedPipeline(ShardedPipelineConfig cfg)
      : cfg_(cfg) {
    if (cfg_.shard_count == 0) {
      cfg_.shard_count = 1;
    }
    workers_.reserve(cfg_.shard_count);
    stats_.per_shard_events.assign(cfg_.shard_count, 0);
    stats_.per_shard_push_retries.assign(cfg_.shard_count, 0);
    stats_.per_shard_worker_wall_ns.assign(cfg_.shard_count, 0);
    stats_.per_shard_start_cpu.assign(cfg_.shard_count, -1);
    stats_.per_shard_ring_address_mod64.assign(cfg_.shard_count, 0);
    stats_.shard_ring_capacity = ShardCapacity;
    for (std::size_t i = 0; i < cfg_.shard_count; ++i) {
      auto worker = std::make_unique<Worker>(cfg_.gap_window, cfg_.per_venue_capacity);
      worker->ring = std::make_unique<mf::core::SPSCRingBuffer<mf::core::BookEvent, ShardCapacity>>();
      stats_.per_shard_ring_address_mod64[i] =
          reinterpret_cast<std::uintptr_t>(worker->ring.get()) & static_cast<std::uintptr_t>(63);
      workers_.push_back(std::move(worker));
    }
    local_seq_by_shard_.assign(cfg_.shard_count, {0, 0, 0});
    for (std::size_t i = 0; i < workers_.size(); ++i) {
      workers_[i]->thread = std::thread([this, i]() { this->worker_loop(i); });
    }
  }

  ~ShardedPipeline() { finalize(); }

  ShardedPipeline(const ShardedPipeline&) = delete;
  ShardedPipeline& operator=(const ShardedPipeline&) = delete;

  bool submit(const mf::core::BookEvent& ev) noexcept {
    mf::core::BookEvent copy = ev;
    copy.ingest_ts_ns = mf::core::monotonic_raw_now_ns();
    const std::size_t shard = shard_for_symbol(copy.symbol, cfg_.shard_count);
    auto& worker = *workers_[shard];
    mf::core::BookEvent worker_ev = copy;
    const std::size_t venue_idx = static_cast<std::size_t>(static_cast<std::uint8_t>(copy.venue));
    worker_ev.sequence = ++local_seq_by_shard_[shard][venue_idx];
    while (!worker.ring->try_push(worker_ev)) {
      ++worker.push_retries;
      if (input_closed_.load(std::memory_order_acquire)) {
        return false;
      }
      std::this_thread::yield();
    }
    submitted_events_.fetch_add(1, std::memory_order_relaxed);
    routed_events_.fetch_add(1, std::memory_order_relaxed);
    update_incremental_reaggregation_crc(copy);
    routed_events_in_submit_order_.push_back(copy);
    return true;
  }

  void close_input() noexcept { input_closed_.store(true, std::memory_order_release); }

  void finalize() {
    if (finalized_.exchange(true, std::memory_order_acq_rel)) {
      return;
    }

    close_input();
    for (auto& worker : workers_) {
      if (worker->thread.joinable()) {
        worker->thread.join();
      }
    }

    stats_.submitted_events = submitted_events_.load(std::memory_order_acquire);
    stats_.routed_events = routed_events_.load(std::memory_order_acquire);
    const auto reagg_push_begin = clock_type::now();
    const bool already_sorted = is_deterministic_ordered(routed_events_in_submit_order_);
    const auto reagg_push_end = clock_type::now();
    for (std::size_t i = 0; i < workers_.size(); ++i) {
      const auto& worker = workers_[i];
      stats_.per_shard_push_retries[i] = worker->push_retries;
      stats_.per_shard_worker_wall_ns[i] = worker->worker_wall_ns;
      stats_.publish_latency_ns.insert(
          stats_.publish_latency_ns.end(),
          worker->publisher.latencies_ns.begin(),
          worker->publisher.latencies_ns.end());
    }

    const auto reagg_drain_begin = clock_type::now();
    if (already_sorted && incremental_crc_ordered_) {
      stats_.reaggregated_crc = incremental_reaggregated_crc_;
      stats_.reaggregator_sorted_input_fast_path = true;
    } else {
      mf::phase2::DeterministicMerger merger(0);
      for (const auto& ev : routed_events_in_submit_order_) {
        (void)merger.push(ev);
      }
      mf::core::BookEvent ev{};
      while (merger.pop_next(ev)) {
        mf::core::update_crc_from_book_event(stats_.reaggregated_crc, ev);
      }
    }
    const auto reagg_drain_end = clock_type::now();
    stats_.reaggregator_push_ns = elapsed_ns(reagg_push_begin, reagg_push_end);
    stats_.reaggregator_drain_ns = elapsed_ns(reagg_drain_begin, reagg_drain_end);
  }

  [[nodiscard]] const ShardedPipelineStats& stats() const noexcept { return stats_; }

 private:
  using clock_type = std::chrono::steady_clock;

  static std::uint64_t elapsed_ns(clock_type::time_point begin, clock_type::time_point end) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
  }

  static int current_cpu() noexcept {
#if defined(__linux__)
    return ::sched_getcpu();
#else
    return -1;
#endif
  }

  static bool less_event(const mf::core::BookEvent& a, const mf::core::BookEvent& b) noexcept {
    if (a.exchange_ts_ns != b.exchange_ts_ns) {
      return a.exchange_ts_ns < b.exchange_ts_ns;
    }
    if (a.venue != b.venue) {
      return static_cast<std::uint8_t>(a.venue) < static_cast<std::uint8_t>(b.venue);
    }
    if (a.sequence != b.sequence) {
      return a.sequence < b.sequence;
    }
    return a.raw_type < b.raw_type;
  }

  static bool is_deterministic_ordered(const std::vector<mf::core::BookEvent>& events) noexcept {
    for (std::size_t i = 1; i < events.size(); ++i) {
      if (less_event(events[i], events[i - 1])) {
        return false;
      }
    }
    return true;
  }

  void update_incremental_reaggregation_crc(const mf::core::BookEvent& ev) noexcept {
    if (has_last_reaggregated_event_ && less_event(ev, last_reaggregated_event_)) {
      incremental_crc_ordered_ = false;
    }
    last_reaggregated_event_ = ev;
    has_last_reaggregated_event_ = true;
    if (incremental_crc_ordered_) {
      mf::core::update_crc_from_book_event(incremental_reaggregated_crc_, ev);
    }
  }

  struct LatencyPublisher final : public mf::phase3::IFeaturePublisher {
    std::vector<std::uint64_t> latencies_ns{};
    bool try_publish(const mf::phase3::FeatureVector& fv) noexcept override {
      const std::uint64_t now_ns = mf::core::monotonic_raw_now_ns();
      if (now_ns >= fv.ingest_ts_ns) {
        latencies_ns.push_back(now_ns - fv.ingest_ts_ns);
      }
      return true;
    }
  };

  struct Worker {
    explicit Worker(std::uint64_t gap_window, std::size_t per_venue_capacity)
        : pipeline(gap_window, per_venue_capacity), bridge(&publisher) {}

    std::unique_ptr<mf::core::SPSCRingBuffer<mf::core::BookEvent, ShardCapacity>> ring{};
    mf::phase2::Pipeline pipeline;
    LatencyPublisher publisher{};
    mf::phase3::FeatureBridge bridge;
    std::thread thread{};
    std::uint64_t processed{0};
    std::uint64_t push_retries{0};
    std::uint64_t worker_wall_ns{0};
  };

  void worker_loop(std::size_t shard_idx) {
    auto& worker = *workers_[shard_idx];
    stats_.per_shard_start_cpu[shard_idx] = current_cpu();
    const auto worker_begin = clock_type::now();
    mf::core::BookEvent ev{};
    std::uint32_t empty_polls = 0;
    while (true) {
      if (worker.ring->try_pop(ev)) {
        empty_polls = 0;
        worker.pipeline.on_event(ev);
        ++worker.processed;
        continue;
      }
      if (input_closed_.load(std::memory_order_acquire)) {
        break;
      }
      ++empty_polls;
      if (empty_polls < 1024U) {
        std::this_thread::yield();
      } else {
        empty_polls = 0;
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
    }
    worker.pipeline.finalize(&worker.bridge);
    const auto worker_end = clock_type::now();
    worker.worker_wall_ns = elapsed_ns(worker_begin, worker_end);
    stats_.per_shard_events[shard_idx] = worker.processed;
  }

  ShardedPipelineConfig cfg_{};
  std::vector<std::unique_ptr<Worker>> workers_{};
  std::vector<std::array<std::uint64_t, 3>> local_seq_by_shard_{};
  std::vector<mf::core::BookEvent> routed_events_in_submit_order_{};
  mf::core::BookEvent last_reaggregated_event_{};
  bool has_last_reaggregated_event_{false};
  bool incremental_crc_ordered_{true};
  std::uint32_t incremental_reaggregated_crc_{0};
  std::atomic<bool> input_closed_{false};
  std::atomic<bool> finalized_{false};
  std::atomic<std::uint64_t> submitted_events_{0};
  std::atomic<std::uint64_t> routed_events_{0};
  ShardedPipelineStats stats_{};
};

inline std::uint32_t deterministic_crc_for_events(const std::vector<mf::core::BookEvent>& events) {
  // Unbounded per-venue capacity so the baseline CRC matches the sharded
  // reaggregator path (which also uses an unbounded DeterministicMerger).
  mf::phase2::Pipeline pipeline(/*gap_window=*/256, /*per_venue_capacity=*/0);
  for (const auto& ev : events) {
    pipeline.on_event(ev);
  }
  pipeline.finalize();
  return pipeline.stats().merged_crc;
}

}  // namespace mf::runtime
