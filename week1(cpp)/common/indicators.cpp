#include "indicators.hpp"
#include <cmath>
#include <limits>

namespace quant::ind {

std::vector<double> sma(const std::vector<double>& prices, size_t period) {
    std::vector<double> result(prices.size(), std::numeric_limits<double>::quiet_NaN());
    if (period == 0 || prices.size() < period) return result;

    double sum = 0.0;
    for (size_t i = 0; i < prices.size(); ++i) {
        sum += prices[i];
        if (i >= period) sum -= prices[i - period];
        if (i >= period - 1) result[i] = sum / static_cast<double>(period);
    }
    return result;
}

std::vector<double> ema(const std::vector<double>& prices, size_t period) {
    std::vector<double> result(prices.size(), std::numeric_limits<double>::quiet_NaN());
    if (period == 0 || prices.empty()) return result;

    double alpha = 2.0 / (static_cast<double>(period) + 1.0);
    result[0] = prices[0];
    for (size_t i = 1; i < prices.size(); ++i) {
        result[i] = alpha * prices[i] + (1.0 - alpha) * result[i - 1];
    }
    return result;
}

MacdResult macd(const std::vector<double>& prices, size_t short_period,
                size_t long_period, size_t signal_period) {
    MacdResult result;
    result.dif.resize(prices.size(), std::numeric_limits<double>::quiet_NaN());
    result.dea.resize(prices.size(), std::numeric_limits<double>::quiet_NaN());
    result.bar.resize(prices.size(), std::numeric_limits<double>::quiet_NaN());

    if (prices.empty()) return result;

    auto ema_short = ema(prices, short_period);
    auto ema_long = ema(prices, long_period);

    std::vector<double> dif(prices.size());
    for (size_t i = 0; i < prices.size(); ++i) {
        dif[i] = ema_short[i] - ema_long[i];
    }

    auto dea = ema(dif, signal_period);

    for (size_t i = 0; i < prices.size(); ++i) {
        result.dif[i] = dif[i];
        result.dea[i] = dea[i];
        result.bar[i] = (dif[i] - dea[i]) * 2.0;
    }
    return result;
}

std::vector<double> rsi(const std::vector<double>& prices, size_t period) {
    std::vector<double> result(prices.size(), std::numeric_limits<double>::quiet_NaN());
    if (period == 0 || prices.size() <= period) return result;

    double gain_sum = 0.0, loss_sum = 0.0;
    for (size_t i = 1; i <= period; ++i) {
        double diff = prices[i] - prices[i - 1];
        if (diff > 0) gain_sum += diff;
        else loss_sum += -diff;
    }

    double avg_gain = gain_sum / static_cast<double>(period);
    double avg_loss = loss_sum / static_cast<double>(period);

    for (size_t i = period; i < prices.size(); ++i) {
        double diff = prices[i] - prices[i - 1];
        double gain = diff > 0 ? diff : 0.0;
        double loss = diff < 0 ? -diff : 0.0;

        avg_gain = (avg_gain * (period - 1) + gain) / static_cast<double>(period);
        avg_loss = (avg_loss * (period - 1) + loss) / static_cast<double>(period);

        if (avg_loss == 0.0) {
            result[i] = 100.0;
        } else {
            double rs = avg_gain / avg_loss;
            result[i] = 100.0 - (100.0 / (1.0 + rs));
        }
    }
    return result;
}

std::vector<double> atr(const std::vector<double>& high,
                        const std::vector<double>& low,
                        const std::vector<double>& close,
                        size_t period) {
    size_t n = high.size();
    std::vector<double> result(n, std::numeric_limits<double>::quiet_NaN());
    if (n == 0 || n != low.size() || n != close.size() || period == 0) return result;

    std::vector<double> tr(n, 0.0);
    tr[0] = high[0] - low[0];
    for (size_t i = 1; i < n; ++i) {
        double tr1 = high[i] - low[i];
        double tr2 = std::abs(high[i] - close[i - 1]);
        double tr3 = std::abs(low[i] - close[i - 1]);
        tr[i] = std::max(tr1, std::max(tr2, tr3));
    }

    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += tr[i];
        if (i >= period) sum -= tr[i - period];
        if (i >= period - 1) result[i] = sum / static_cast<double>(period);
    }
    return result;
}

} // namespace quant::ind
