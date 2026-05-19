#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_set>
#include <vector>

#include "mf/core/types.hpp"

namespace mf::phase3 {

class OrderBookEngine;

struct BookLevel {
  std::uint32_t price{0};
  std::uint64_t qty{0};
};

struct BookSnapshot {
  std::vector<BookLevel> bids{};
  std::vector<BookLevel> asks{};
};

inline bool byte_equal(const BookLevel& a, const BookLevel& b) noexcept {
  return std::memcmp(&a.price, &b.price, sizeof(a.price)) == 0 &&
         std::memcmp(&a.qty, &b.qty, sizeof(a.qty)) == 0;
}

inline bool byte_equal(const BookSnapshot& a, const BookSnapshot& b) noexcept {
  if (a.bids.size() != b.bids.size() || a.asks.size() != b.asks.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.bids.size(); ++i) {
    if (!byte_equal(a.bids[i], b.bids[i])) {
      return false;
    }
  }
  for (std::size_t i = 0; i < a.asks.size(); ++i) {
    if (!byte_equal(a.asks[i], b.asks[i])) {
      return false;
    }
  }
  return true;
}

struct BookReconcileStats {
  std::uint64_t events_processed{0};
  std::uint64_t snapshots_checked{0};
  std::uint64_t divergences{0};
  std::uint64_t snapshot_interval{0};
};

class BookReconciler {
 public:
  explicit BookReconciler(std::uint64_t snapshot_interval);
  ~BookReconciler();

  BookReconciler(const BookReconciler&) = delete;
  BookReconciler& operator=(const BookReconciler&) = delete;

  BookReconcileStats on_event(const mf::core::BookEvent& ev);
  [[nodiscard]] const BookReconcileStats& stats() const noexcept { return stats_; }
  [[nodiscard]] const OrderBookEngine& live_engine() const noexcept;

 private:
  struct VenueSymbol {
    mf::core::Venue venue{mf::core::Venue::Nasdaq};
    std::uint64_t symbol{0};

    bool operator==(const VenueSymbol& other) const noexcept {
      return venue == other.venue && symbol == other.symbol;
    }
  };

  struct VenueSymbolHash {
    std::size_t operator()(const VenueSymbol& v) const noexcept {
      return (static_cast<std::size_t>(static_cast<std::uint8_t>(v.venue)) << 56U) ^
             static_cast<std::size_t>(v.symbol);
    }
  };

  void check_snapshot();

  std::uint64_t snapshot_interval_{1};
  std::unique_ptr<OrderBookEngine> live_{};
  std::vector<mf::core::BookEvent> consumed_{};
  std::unordered_set<VenueSymbol, VenueSymbolHash> touched_{};
  BookReconcileStats stats_{};
};

}  // namespace mf::phase3
