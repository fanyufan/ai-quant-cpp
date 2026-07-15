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

struct BollingerResult {
    std::vector<double> upper;
    std::vector<double> middle;
    std::vector<double> lower;
};
BollingerResult bollinger(const std::vector<double>& prices,
                          size_t period = 20,
                          double k = 2.0);

// 乖离率 BIAS = (price - ma) / ma
std::vector<double> bias(const std::vector<double>& prices, size_t period);

// 动量 / ROC = (price - price[n]) / price[n] * 100
std::vector<double> roc(const std::vector<double>& prices, size_t period = 10);
std::vector<double> momentum(const std::vector<double>& prices, size_t period = 10);

// ADX (Average Directional Index)，返回 +DI, -DI, DX, ADX
struct AdxResult {
    std::vector<double> plus_di;
    std::vector<double> minus_di;
    std::vector<double> dx;
    std::vector<double> adx;
};
AdxResult adx(const std::vector<double>& high,
              const std::vector<double>& low,
              const std::vector<double>& close,
              size_t period = 14);

// Slow Stochastic (KDJ style): fastk_period, slowk_period, slowd_period
struct StochResult {
    std::vector<double> k;
    std::vector<double> d;
};
StochResult stoch(const std::vector<double>& high,
                  const std::vector<double>& low,
                  const std::vector<double>& close,
                  size_t fastk_period = 9,
                  size_t slowk_period = 3,
                  size_t slowd_period = 3);

// Commodity Channel Index
std::vector<double> cci(const std::vector<double>& high,
                        const std::vector<double>& low,
                        const std::vector<double>& close,
                        size_t period = 14);

// Williams %R
std::vector<double> willr(const std::vector<double>& high,
                          const std::vector<double>& low,
                          const std::vector<double>& close,
                          size_t period = 14);

// On Balance Volume
std::vector<double> obv(const std::vector<double>& close,
                        const std::vector<double>& volume);

} // namespace quant::ind
