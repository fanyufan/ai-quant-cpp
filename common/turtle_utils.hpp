#pragma once

#include <vector>
#include <limits>
#include "backtest.hpp"

namespace quant::turtle {

// Rolling max of the previous 'period' bars, excluding current bar.
// result[i] = max(values[i-period .. i-1]), NaN if i < period.
std::vector<double> rolling_max_prev(const std::vector<double>& values, size_t period);

// Rolling min of the previous 'period' bars, excluding current bar.
std::vector<double> rolling_min_prev(const std::vector<double>& values, size_t period);

// ATR-based unit size, rounded down to lot_size.
int calc_unit_size(double portfolio_value, double atr, double risk_pct, int lot_size = 100);

} // namespace quant::turtle
