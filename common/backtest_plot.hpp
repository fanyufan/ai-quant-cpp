#pragma once

#include <string>
#include <vector>
#include "backtest.hpp"

namespace quant::bt {

// Plot price+trades, NAV vs benchmark, and drawdown. Saves PNG to output_path.
void plot_backtest(const Result& result,
                   const std::vector<Bar>& bars,
                   const std::string& stock_code,
                   const std::string& title,
                   const std::string& output_path);

} // namespace quant::bt
