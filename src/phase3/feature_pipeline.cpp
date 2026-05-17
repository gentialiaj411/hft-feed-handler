#include "mf/phase3/feature_pipeline.hpp"

#include <cstddef>
#include <cmath>
namespace mf::phase3 {

FeaturePipeline::FeaturePipeline() {
  by_symbol_.reserve(1024);
}

FeaturePipeline::FeaturePipeline(Config cfg) : cfg_(cfg) {
  by_symbol_.reserve(1024);
}

std::optional<FeatureVector> FeaturePipeline::on_event(
    const mf::core::BookEvent& ev,
    const Nbbo& nbbo,
    double queue_ahead_hint) noexcept {
  const std::uint64_t symbol = ev.symbol.as_u64();
  State* state = nullptr;
  if (cached_valid_ && cached_symbol_ == symbol && cached_state_ != nullptr) {
    state = cached_state_;
  } else {
    auto it = by_symbol_.find(symbol);
    if (it == by_symbol_.end()) {
      cached_valid_ = false;  // invalidate before potential rehash
      it = by_symbol_.emplace(symbol, State{}).first;
      if (it->second.ofi_window.capacity() == 0U) {
        it->second.ofi_window.reserve(cfg_.ofi_reserve_points);
      }
    }
    cached_symbol_ = symbol;
    cached_state_ = &it->second;
    cached_valid_ = true;
    state = &it->second;
  }
  auto& s = *state;

  if (!nbbo.has_bid || !nbbo.has_ask || nbbo.bid_price == 0 || nbbo.ask_price == 0) {
    return std::nullopt;
  }

  const double bid_p = static_cast<double>(nbbo.bid_price);
  const double ask_p = static_cast<double>(nbbo.ask_price);
  const double bid_q = static_cast<double>(nbbo.bid_qty);
  const double ask_q = static_cast<double>(nbbo.ask_qty);
  const double mid = 0.5 * (bid_p + ask_p);
  const double denom = bid_q + ask_q;
  const double micro = (denom > 0.0) ? ((ask_p * bid_q + bid_p * ask_q) / denom) : mid;

  double delta_ofi = 0.0;
  if (s.last_bid_price != 0 || s.last_ask_price != 0) {
    if (nbbo.bid_price > s.last_bid_price) {
      delta_ofi += bid_q;
    } else if (nbbo.bid_price < s.last_bid_price) {
      delta_ofi -= static_cast<double>(s.last_bid_qty);
    } else {
      delta_ofi += (bid_q - static_cast<double>(s.last_bid_qty));
    }

    if (nbbo.ask_price < s.last_ask_price) {
      delta_ofi -= ask_q;
    } else if (nbbo.ask_price > s.last_ask_price) {
      delta_ofi += static_cast<double>(s.last_ask_qty);
    } else {
      delta_ofi -= (ask_q - static_cast<double>(s.last_ask_qty));
    }
  }
  s.last_bid_price = nbbo.bid_price;
  s.last_bid_qty = nbbo.bid_qty;
  s.last_ask_price = nbbo.ask_price;
  s.last_ask_qty = nbbo.ask_qty;

  s.ofi_sum += delta_ofi;
  s.ofi_window.push_back(State::OfiPoint{ev.exchange_ts_ns, delta_ofi});
  while (s.ofi_head < s.ofi_window.size() &&
         (ev.exchange_ts_ns - s.ofi_window[s.ofi_head].ts) > cfg_.ofi_window_ns) {
    s.ofi_sum -= s.ofi_window[s.ofi_head].value;
    ++s.ofi_head;
  }
  if (s.ofi_head > 4096U && s.ofi_head * 2U > s.ofi_window.size()) {
    s.ofi_window.erase(s.ofi_window.begin(), s.ofi_window.begin() + static_cast<std::ptrdiff_t>(s.ofi_head));
    s.ofi_head = 0;
  }

  if (ev.type == mf::core::EventType::Trade || ev.type == mf::core::EventType::CrossTrade) {
    const double trade_p = static_cast<double>(ev.price);
    const double eff = 2.0 * std::fabs(trade_p - mid);
    if (!s.has_effective_spread) {
      s.effective_spread_ema = eff;
      s.has_effective_spread = true;
    } else {
      s.effective_spread_ema = 0.95 * s.effective_spread_ema + 0.05 * eff;
    }

    const double sign = (ev.side == mf::core::Side::Buy) ? 1.0 : ((ev.side == mf::core::Side::Sell) ? -1.0 : 0.0);
    const double x = sign * static_cast<double>(ev.qty);
    const double y = s.has_mid ? (mid - s.last_mid) : 0.0;
    s.reg_n += 1;
    s.reg_sum_x += x;
    s.reg_sum_y += y;
    s.reg_sum_xx += x * x;
    s.reg_sum_xy += x * y;

    if (sign > 0.0) {
      s.vpin_bucket_buy += ev.qty;
    } else if (sign < 0.0) {
      s.vpin_bucket_sell += ev.qty;
    }
    while ((s.vpin_bucket_buy + s.vpin_bucket_sell) >= cfg_.vpin_bucket_volume) {
      const double tox = std::fabs(static_cast<double>(s.vpin_bucket_buy) - static_cast<double>(s.vpin_bucket_sell)) /
                         static_cast<double>(cfg_.vpin_bucket_volume);
      const std::size_t max_buckets = (cfg_.vpin_bucket_count < s.vpin_buckets.size())
                                          ? cfg_.vpin_bucket_count
                                          : s.vpin_buckets.size();
      if (s.vpin_size < max_buckets) {
        s.vpin_buckets[(s.vpin_head + s.vpin_size) % max_buckets] = tox;
        ++s.vpin_size;
      } else if (max_buckets > 0U) {
        s.vpin_buckets[s.vpin_head] = tox;
        s.vpin_head = (s.vpin_head + 1U) % max_buckets;
      }
      const std::uint32_t excess = (s.vpin_bucket_buy + s.vpin_bucket_sell) - cfg_.vpin_bucket_volume;
      s.vpin_bucket_buy = (s.vpin_bucket_buy > excess) ? excess : 0U;
      s.vpin_bucket_sell = (s.vpin_bucket_sell > s.vpin_bucket_buy) ? (excess - s.vpin_bucket_buy) : 0U;
    }
  }

  s.last_mid = mid;
  s.has_mid = true;

  double lambda = 0.0;
  const double denom_reg = static_cast<double>(s.reg_n) * s.reg_sum_xx - s.reg_sum_x * s.reg_sum_x;
  if (std::fabs(denom_reg) > 1e-12) {
    lambda = (static_cast<double>(s.reg_n) * s.reg_sum_xy - s.reg_sum_x * s.reg_sum_y) / denom_reg;
  }

  double vpin = 0.0;
  if (s.vpin_size > 0U) {
    const std::size_t max_buckets = (cfg_.vpin_bucket_count < s.vpin_buckets.size())
                                        ? cfg_.vpin_bucket_count
                                        : s.vpin_buckets.size();
    double sum = 0.0;
    for (std::size_t i = 0; i < s.vpin_size; ++i) {
      sum += s.vpin_buckets[(s.vpin_head + i) % max_buckets];
    }
    vpin = sum / static_cast<double>(s.vpin_size);
  }

  FeatureVector fv{};
  fv.symbol_u64 = symbol;
  fv.exchange_ts_ns = ev.exchange_ts_ns;
  fv.ingest_ts_ns = ev.ingest_ts_ns;
  fv.nbbo_bid_price = nbbo.bid_price;
  fv.nbbo_bid_qty = nbbo.bid_qty;
  fv.nbbo_ask_price = nbbo.ask_price;
  fv.nbbo_ask_qty = nbbo.ask_qty;
  fv.microprice = micro;
  fv.ofi = s.ofi_sum;
  fv.queue_ahead = queue_ahead_hint;
  fv.effective_spread = s.effective_spread_ema;
  fv.kyle_lambda = lambda;
  fv.vpin = vpin;
  return fv;
}

}  // namespace mf::phase3
