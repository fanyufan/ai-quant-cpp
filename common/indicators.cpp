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

BollingerResult bollinger(const std::vector<double>& prices, size_t period, double k) {
    BollingerResult result;
    size_t n = prices.size();
    result.upper.resize(n, std::numeric_limits<double>::quiet_NaN());
    result.middle.resize(n, std::numeric_limits<double>::quiet_NaN());
    result.lower.resize(n, std::numeric_limits<double>::quiet_NaN());
    if (period == 0 || n < period) return result;

    auto middle = sma(prices, period);
    for (size_t i = period - 1; i < n; ++i) {
        double sum = 0.0;
        for (size_t j = i + 1 - period; j <= i; ++j) {
            double diff = prices[j] - middle[i];
            sum += diff * diff;
        }
        double stddev = std::sqrt(sum / static_cast<double>(period));
        result.middle[i] = middle[i];
        result.upper[i] = middle[i] + k * stddev;
        result.lower[i] = middle[i] - k * stddev;
    }
    return result;
}

std::vector<double> bias(const std::vector<double>& prices, size_t period) {
    std::vector<double> result(prices.size(), std::numeric_limits<double>::quiet_NaN());
    if (period == 0 || prices.size() < period) return result;

    auto ma = sma(prices, period);
    for (size_t i = period - 1; i < prices.size(); ++i) {
        if (ma[i] != 0.0) {
            result[i] = (prices[i] - ma[i]) / ma[i];
        }
    }
    return result;
}

std::vector<double> roc(const std::vector<double>& prices, size_t period) {
    std::vector<double> result(prices.size(), std::numeric_limits<double>::quiet_NaN());
    if (period == 0 || prices.size() <= period) return result;
    for (size_t i = period; i < prices.size(); ++i) {
        if (prices[i - period] != 0.0) {
            result[i] = (prices[i] - prices[i - period]) / prices[i - period] * 100.0;
        }
    }
    return result;
}

std::vector<double> momentum(const std::vector<double>& prices, size_t period) {
    return roc(prices, period);
}

AdxResult adx(const std::vector<double>& high,
              const std::vector<double>& low,
              const std::vector<double>& close,
              size_t period) {
    size_t n = high.size();
    AdxResult result;
    result.plus_di.resize(n, std::numeric_limits<double>::quiet_NaN());
    result.minus_di.resize(n, std::numeric_limits<double>::quiet_NaN());
    result.dx.resize(n, std::numeric_limits<double>::quiet_NaN());
    result.adx.resize(n, std::numeric_limits<double>::quiet_NaN());

    if (n == 0 || n != low.size() || n != close.size() || period == 0 || n <= period) {
        return result;
    }

    std::vector<double> tr(n), plus_dm(n), minus_dm(n);
    tr[0] = high[0] - low[0];
    plus_dm[0] = 0.0;
    minus_dm[0] = 0.0;
    for (size_t i = 1; i < n; ++i) {
        double up = high[i] - high[i - 1];
        double down = low[i - 1] - low[i];
        plus_dm[i] = (up > down && up > 0.0) ? up : 0.0;
        minus_dm[i] = (down > up && down > 0.0) ? down : 0.0;
        double tr1 = high[i] - low[i];
        double tr2 = std::abs(high[i] - close[i - 1]);
        double tr3 = std::abs(low[i] - close[i - 1]);
        tr[i] = std::max(tr1, std::max(tr2, tr3));
    }

    // Wilder smoothing
    auto smooth = [&](const std::vector<double>& src) {
        std::vector<double> out(n, std::numeric_limits<double>::quiet_NaN());
        double sum = 0.0;
        for (size_t i = 1; i <= period; ++i) sum += src[i];
        out[period] = sum;
        for (size_t i = period + 1; i < n; ++i) {
            out[i] = out[i - 1] - (out[i - 1] / static_cast<double>(period)) + src[i];
        }
        return out;
    };

    auto atr_smoothed = smooth(tr);
    auto plus_dm_smoothed = smooth(plus_dm);
    auto minus_dm_smoothed = smooth(minus_dm);

    for (size_t i = period; i < n; ++i) {
        if (atr_smoothed[i] > 0.0) {
            result.plus_di[i] = (plus_dm_smoothed[i] / atr_smoothed[i]) * 100.0;
            result.minus_di[i] = (minus_dm_smoothed[i] / atr_smoothed[i]) * 100.0;
        }
        if (result.plus_di[i] + result.minus_di[i] > 0.0) {
            result.dx[i] = std::abs(result.plus_di[i] - result.minus_di[i]) /
                           (result.plus_di[i] + result.minus_di[i]) * 100.0;
        }
    }

    // Smooth DX to get ADX
    double dx_sum = 0.0;
    for (size_t i = period; i < period + period && i < n; ++i) {
        if (!std::isnan(result.dx[i])) dx_sum += result.dx[i];
    }
    if (n >= 2 * period) {
        result.adx[2 * period - 1] = dx_sum / static_cast<double>(period);
        for (size_t i = 2 * period; i < n; ++i) {
            result.adx[i] = (result.adx[i - 1] * (period - 1) + result.dx[i]) / static_cast<double>(period);
        }
    }
    return result;
}

} // namespace quant::ind
