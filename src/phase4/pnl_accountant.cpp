#include "mf/phase4/pnl_accountant.hpp"

#include <cmath>
#include <cstdlib>

namespace mf::phase4 {

PnlAccountant::PnlAccountant() : PnlAccountant(Config{}) {}
PnlAccountant::PnlAccountant(Config cfg) : cfg_(cfg) {}

void PnlAccountant::on_order_submitted() {
  ++submitted_orders_;
}

void PnlAccountant::on_fill(std::uint64_t symbol_u64, const Fill& fill, bool taker) {
  auto& st = by_symbol_[symbol_u64];
  const double px = static_cast<double>(fill.price);
  const double qty = static_cast<double>(fill.qty);
  const std::int64_t signed_qty = (fill.side == OrderSide::Buy) ? static_cast<std::int64_t>(fill.qty)
                                                                 : -static_cast<std::int64_t>(fill.qty);

  if (st.position == 0 || ((st.position > 0) == (signed_qty > 0))) {
    const double old_notional = st.avg_cost * static_cast<double>(std::llabs(st.position));
    const double new_notional = old_notional + px * qty;
    st.position += signed_qty;
    st.avg_cost = (st.position == 0) ? 0.0 : (new_notional / static_cast<double>(std::llabs(st.position)));
  } else {
    const std::uint64_t close_qty = static_cast<std::uint64_t>(std::min<std::int64_t>(std::llabs(st.position), std::llabs(signed_qty)));
    const double pnl = (st.position > 0) ? ((px - st.avg_cost) * static_cast<double>(close_qty))
                                         : ((st.avg_cost - px) * static_cast<double>(close_qty));
    st.realized_pnl += pnl;
    st.position += signed_qty;
    if (st.position == 0) {
      st.avg_cost = 0.0;
    } else if ((st.position > 0) == (signed_qty > 0) && std::llabs(st.position) > static_cast<std::int64_t>(close_qty)) {
      st.avg_cost = px;
    }
  }

  st.cash += (fill.side == OrderSide::Buy) ? (-px * qty) : (px * qty);
  st.gross_volume += px * qty;
  ++st.fill_count;
  ++total_fills_;
  if (taker) {
    cumulative_fees_ += cfg_.taker_fee_per_share * qty;
  } else {
    cumulative_rebates_ += cfg_.maker_rebate_per_share * qty;
  }
}

void PnlAccountant::on_feature(const mf::phase3::FeatureVector& fv) {
  if (fv.nbbo_bid_price == 0 || fv.nbbo_ask_price == 0) {
    return;
  }
  auto& st = by_symbol_[fv.symbol_u64];
  st.last_mid = 0.5 * (static_cast<double>(fv.nbbo_bid_price) + static_cast<double>(fv.nbbo_ask_price));
  st.has_mid = true;
}

void PnlAccountant::on_tick_end(std::uint64_t ts_ns) {
  double equity = 0.0;
  for (auto& [_, st] : by_symbol_) {
    st.unrealized_pnl = st.has_mid ? ((st.last_mid - st.avg_cost) * static_cast<double>(st.position)) : 0.0;
    equity += st.cash + st.realized_pnl + st.unrealized_pnl;
  }
  if (equity > peak_equity_) {
    peak_equity_ = equity;
  }
  const double dd = peak_equity_ - equity;
  if (dd > max_drawdown_) {
    max_drawdown_ = dd;
  }

  const std::uint64_t bucket = cfg_.sharpe_bucket_ns == 0 ? 0 : (ts_ns / cfg_.sharpe_bucket_ns);
  if (!has_bucket_) {
    has_bucket_ = true;
    current_bucket_ = bucket;
    last_bucket_equity_ = equity;
  } else if (bucket != current_bucket_) {
    const double ret = equity - last_bucket_equity_;
    sum_returns_ += ret;
    sum_sq_returns_ += ret * ret;
    ++n_returns_;
    current_bucket_ = bucket;
    last_bucket_equity_ = equity;
  }
}

PnlAccountant::Report PnlAccountant::finalize() const {
  Report r{};
  for (const auto& [_, st] : by_symbol_) {
    r.total_realized_pnl += st.realized_pnl;
    r.total_unrealized_pnl += st.unrealized_pnl;
    r.fills += st.fill_count;
    r.turnover += st.gross_volume;
    r.total_equity += st.cash + st.realized_pnl + st.unrealized_pnl;
  }
  r.max_drawdown = max_drawdown_;
  r.submitted_orders = submitted_orders_;
  r.fill_ratio = submitted_orders_ == 0 ? 0.0 : static_cast<double>(total_fills_) / static_cast<double>(submitted_orders_);
  r.cumulative_fees = cumulative_fees_;
  r.cumulative_rebates = cumulative_rebates_;
  r.total_equity = r.total_equity - r.cumulative_fees + r.cumulative_rebates;

  if (n_returns_ > 1) {
    const double mean = sum_returns_ / static_cast<double>(n_returns_);
    const double var = (sum_sq_returns_ / static_cast<double>(n_returns_)) - mean * mean;
    if (var > 0.0) {
      r.sharpe = mean / std::sqrt(var);
    }
  }
  return r;
}

}  // namespace mf::phase4
