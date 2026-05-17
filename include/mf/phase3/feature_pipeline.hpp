#pragma once

#include <cstdint>
#include <array>
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

#include "mf/core/types.hpp"
#include "mf/phase3/types.hpp"

namespace mf::phase3 {

class FeaturePipeline {
 public:
  struct Config {
    std::uint64_t ofi_window_ns{1'000'000'000ULL};
    std::uint32_t vpin_bucket_volume{10'000U};
    std::size_t vpin_bucket_count{32};
    std::size_t ofi_reserve_points{1U << 20U};
  };

  FeaturePipeline();
  explicit FeaturePipeline(Config cfg);

  [[nodiscard]] std::optional<FeatureVector> on_event(
      const mf::core::BookEvent& ev,
      const Nbbo& nbbo,
      double queue_ahead_hint) noexcept;

 private:
  struct State {
    struct OfiPoint {
      std::uint64_t ts{0};
      double value{0.0};
    };

    std::uint32_t last_bid_price{0};
    std::uint32_t last_bid_qty{0};
    std::uint32_t last_ask_price{0};
    std::uint32_t last_ask_qty{0};
    double ofi_sum{0.0};
    std::vector<OfiPoint> ofi_window{};
    std::size_t ofi_head{0};
    double effective_spread_ema{0.0};
    bool has_effective_spread{false};
    double last_mid{0.0};
    bool has_mid{false};

    std::uint64_t reg_n{0};
    double reg_sum_x{0.0};
    double reg_sum_y{0.0};
    double reg_sum_xx{0.0};
    double reg_sum_xy{0.0};

    std::uint32_t vpin_bucket_buy{0};
    std::uint32_t vpin_bucket_sell{0};
    std::array<double, 64> vpin_buckets{};
    std::size_t vpin_head{0};
    std::size_t vpin_size{0};
  };
  Config cfg_{};
  std::unordered_map<std::uint64_t, State> by_symbol_{};
  std::uint64_t cached_symbol_{0};
  State* cached_state_{nullptr};
  bool cached_valid_{false};
};

}  // namespace mf::phase3
