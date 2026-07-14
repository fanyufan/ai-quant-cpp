#pragma once

#include <vector>
#include <cstddef>

namespace quant::ind {

std::vector<double> sma(const std::vector<double>& prices, size_t period);
std::vector<double> ema(const std::vector<double>& prices, size_t period);
inline std::vector<double> ma(const std::vector<double>& prices, size_t period) { return sma(prices, period); }

struct MacdResult {
    std::vector<double> dif;
    std::vector<double> dea;
    std::vector<double> bar;
};
MacdResult macd(const std::vector<double>& prices, size_t short_period = 12,
                size_t long_period = 26, size_t signal_period = 9);

std::vector<double> rsi(const std::vector<double>& prices, size_t period = 14);
std::vector<double> atr(const std::vector<double>& high,
                        const std::vector<double>& low,
                        const std::vector<double>& close,
                        size_t period = 14);

} // namespace quant::ind
