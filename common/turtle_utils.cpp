#include "turtle_utils.hpp"

#include <algorithm>
#include <cmath>

namespace quant::turtle {

std::vector<double> rolling_max_prev(const std::vector<double>& values, size_t period) {
    std::vector<double> result(values.size(), std::numeric_limits<double>::quiet_NaN());
    if (period == 0) return result;
    for (size_t i = period; i < values.size(); ++i) {
        double mx = values[i - period];
        for (size_t j = i - period + 1; j < i; ++j) {
            if (values[j] > mx) mx = values[j];
        }
        result[i] = mx;
    }
    return result;
}

std::vector<double> rolling_min_prev(const std::vector<double>& values, size_t period) {
    std::vector<double> result(values.size(), std::numeric_limits<double>::quiet_NaN());
    if (period == 0) return result;
    for (size_t i = period; i < values.size(); ++i) {
        double mn = values[i - period];
        for (size_t j = i - period + 1; j < i; ++j) {
            if (values[j] < mn) mn = values[j];
        }
        result[i] = mn;
    }
    return result;
}

int calc_unit_size(double portfolio_value, double atr, double risk_pct, int lot_size) {
    if (atr <= 0.0 || portfolio_value <= 0.0 || lot_size <= 0) return 0;
    double raw = (portfolio_value * risk_pct) / atr;
    int units = static_cast<int>(raw / lot_size) * lot_size;
    return std::max(units, lot_size);
}

} // namespace quant::turtle
