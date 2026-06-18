#include <cassert>
#include <cmath>
#include <vector>

#include "mf/research/alpha_lab/cost_model.hpp"
#include "mf/research/alpha_lab/signal_registry.hpp"
#include "mf/research/alpha_lab/types.hpp"

int main() {
  const auto zoo = mf::research::alpha_lab::SignalRegistry::default_zoo();
  assert(zoo.size() >= 12);

  mf::research::alpha_lab::LabeledFeatureRow row{};
  row.mid = 100.0;
  row.microprice = 101.0;
  row.ofi = 50.0;
  row.nbbo_bid_price = 9900;
  row.nbbo_ask_price = 10100;
  row.nbbo_bid_qty = 100;
  row.nbbo_ask_qty = 100;
  row.effective_spread = 2.0;
  row.queue_ahead = 10.0;

  mf::research::alpha_lab::SignalRegistry registry{};
  for (const auto& signal : zoo) {
    const std::vector<mf::research::alpha_lab::LabeledFeatureRow> rows = {row};
    const auto values = registry.extract(signal, rows);
    assert(values.size() == 1);
    assert(std::isfinite(values[0]));
  }

  mf::research::alpha_lab::CostModel costs{};
  const double gross = 0.01;
  const double net_small = costs.net_return(gross, 2.0, 0.1);
  const double net_large = costs.net_return(gross, 2.0, 10.0);
  assert(net_large < net_small);
  return 0;
}
