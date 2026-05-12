#pragma once

#include <cstdint>

namespace mf::phase3 {

struct TopOfBook {
  bool has_bid{false};
  bool has_ask{false};
  std::uint32_t bid_price{0};
  std::uint32_t bid_qty{0};
  std::uint32_t ask_price{0};
  std::uint32_t ask_qty{0};
};

struct Nbbo {
  bool has_bid{false};
  bool has_ask{false};
  std::uint32_t bid_price{0};
  std::uint32_t bid_qty{0};
  std::uint32_t ask_price{0};
  std::uint32_t ask_qty{0};
  std::uint8_t bid_venue{0};
  std::uint8_t ask_venue{0};
};

struct FeatureVector {
  std::uint64_t symbol_u64{0};
  std::uint64_t exchange_ts_ns{0};
  std::uint64_t ingest_ts_ns{0};
  std::uint32_t nbbo_bid_price{0};
  std::uint32_t nbbo_bid_qty{0};
  std::uint32_t nbbo_ask_price{0};
  std::uint32_t nbbo_ask_qty{0};
  double microprice{0.0};
  double ofi{0.0};
  double queue_ahead{0.0};
  double effective_spread{0.0};
  double kyle_lambda{0.0};
  double vpin{0.0};
};

}  // namespace mf::phase3
