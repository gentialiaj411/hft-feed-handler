#include "mf/research/signals/ofi.hpp"

namespace mf::research {

OfiSignal::OfiSignal() : OfiSignal(Config{}) {}
OfiSignal::OfiSignal(Config cfg) : cfg_(cfg) {}

void OfiSignal::prune(std::uint64_t ts_ns) {
  if (cfg_.max_window_ns > 0) {
    while (!window_.empty() && window_.front().ts_ns + cfg_.max_window_ns < ts_ns) {
      sum_ -= window_.front().v;
      window_.pop_front();
    }
  }
  while (cfg_.max_events > 0 && window_.size() > cfg_.max_events) {
    sum_ -= window_.front().v;
    window_.pop_front();
  }
}

void OfiSignal::update(const mf::core::BookEvent& ev) {
  double fi = 0.0;

  if (ev.side == mf::core::Side::Buy && ev.price > 0) {
    const std::uint32_t new_px = ev.price;
    const std::uint32_t new_qty = ev.qty;
    if (!has_bid_) {
      fi = static_cast<double>(new_qty);
      has_bid_ = true;
    } else if (new_px > bid_px_) {
      fi = static_cast<double>(new_qty);
    } else if (new_px == bid_px_) {
      fi = static_cast<double>(new_qty) - static_cast<double>(bid_qty_);
    } else {
      fi = -static_cast<double>(bid_qty_);
    }
    bid_px_ = new_px;
    bid_qty_ = new_qty;
  } else if (ev.side == mf::core::Side::Sell && ev.price > 0) {
    const std::uint32_t new_px = ev.price;
    const std::uint32_t new_qty = ev.qty;
    if (!has_ask_) {
      fi = -static_cast<double>(new_qty);
      has_ask_ = true;
    } else if (new_px > ask_px_) {
      fi = -static_cast<double>(new_qty);
    } else if (new_px == ask_px_) {
      fi = -static_cast<double>(new_qty) + static_cast<double>(ask_qty_);
    } else {
      fi = static_cast<double>(ask_qty_);
    }
    ask_px_ = new_px;
    ask_qty_ = new_qty;
  }

  window_.push_back(Entry{ev.exchange_ts_ns, fi});
  sum_ += fi;
  prune(ev.exchange_ts_ns);
}

}  // namespace mf::research
