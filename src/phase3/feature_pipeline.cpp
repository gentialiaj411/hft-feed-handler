#include "mf/phase3/feature_pipeline.hpp"

#include <cmath>
namespace mf::phase3 {

std::optional<FeatureVector> FeaturePipeline::on_event(
    const mf::core::BookEvent& ev,
    const Nbbo& nbbo,
    double queue_ahead_hint) noexcept {
  const std::uint64_t symbol = ev.symbol.as_u64();
  auto& s = by_symbol_[symbol];

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
    if (nbbo.bid_price > s.last_bid_price) delta_ofi += bid_q;
    if (nbbo.bid_price < s.last_bid_price) delta_ofi -= static_cast<double>(s.last_bid_qty);
    if (nbbo.ask_price < s.last_ask_price) delta_ofi -= ask_q;
    if (nbbo.ask_price > s.last_ask_price) delta_ofi += static_cast<double>(s.last_ask_qty);
    delta_ofi += (bid_q - static_cast<double>(s.last_bid_qty));
    delta_ofi -= (ask_q - static_cast<double>(s.last_ask_qty));
  }
  s.last_bid_price = nbbo.bid_price;
  s.last_bid_qty = nbbo.bid_qty;
  s.last_ask_price = nbbo.ask_price;
  s.last_ask_qty = nbbo.ask_qty;

  s.ofi_sum += delta_ofi;
  s.ofi_window.push_back(State::OfiPoint{ev.exchange_ts_ns, delta_ofi});
  while (!s.ofi_window.empty() && (ev.exchange_ts_ns - s.ofi_window.front().ts) > cfg_.ofi_window_ns) {
    s.ofi_sum -= s.ofi_window.front().value;
    s.ofi_window.pop_front();
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
      s.vpin_buckets.push_back(tox);
      if (s.vpin_buckets.size() > cfg_.vpin_bucket_count) {
        s.vpin_buckets.pop_front();
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
  if (!s.vpin_buckets.empty()) {
    double sum = 0.0;
    for (double t : s.vpin_buckets) {
      sum += t;
    }
    vpin = sum / static_cast<double>(s.vpin_buckets.size());
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
