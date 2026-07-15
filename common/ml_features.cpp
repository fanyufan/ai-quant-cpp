#include "ml_features.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <numeric>

#include "indicators.hpp"

namespace quant::ml {

namespace {

const double NaN = std::numeric_limits<double>::quiet_NaN();

std::vector<double> rolling_max(const std::vector<double>& vals, size_t period) {
    std::vector<double> out(vals.size(), NaN);
    if (vals.empty() || period == 0) return out;
    std::deque<size_t> dq;
    for (size_t i = 0; i < vals.size(); ++i) {
        while (!dq.empty() && vals[dq.back()] <= vals[i]) dq.pop_back();
        dq.push_back(i);
        while (!dq.empty() && dq.front() + period <= i) dq.pop_front();
        if (i + 1 >= period) out[i] = vals[dq.front()];
    }
    return out;
}

std::vector<double> rolling_min(const std::vector<double>& vals, size_t period) {
    std::vector<double> out(vals.size(), NaN);
    if (vals.empty() || period == 0) return out;
    std::deque<size_t> dq;
    for (size_t i = 0; i < vals.size(); ++i) {
        while (!dq.empty() && vals[dq.back()] >= vals[i]) dq.pop_back();
        dq.push_back(i);
        while (!dq.empty() && dq.front() + period <= i) dq.pop_front();
        if (i + 1 >= period) out[i] = vals[dq.front()];
    }
    return out;
}

std::vector<double> rolling_mean(const std::vector<double>& vals, size_t period) {
    std::vector<double> out(vals.size(), NaN);
    if (vals.empty() || period == 0) return out;
    double sum = 0.0;
    for (size_t i = 0; i < vals.size(); ++i) {
        sum += vals[i];
        if (i >= period) sum -= vals[i - period];
        if (i >= period - 1) out[i] = sum / static_cast<double>(period);
    }
    return out;
}

std::vector<double> rolling_std(const std::vector<double>& vals, size_t period) {
    std::vector<double> out(vals.size(), NaN);
    if (vals.empty() || period == 0 || vals.size() < period) return out;
    for (size_t i = period - 1; i < vals.size(); ++i) {
        double sum = 0.0;
        for (size_t j = i + 1 - period; j <= i; ++j) sum += vals[j];
        double mean = sum / static_cast<double>(period);
        double sq = 0.0;
        for (size_t j = i + 1 - period; j <= i; ++j) {
            double d = vals[j] - mean;
            sq += d * d;
        }
        out[i] = std::sqrt(sq / static_cast<double>(period));
    }
    return out;
}

std::vector<double> rolling_corr(const std::vector<double>& a,
                                 const std::vector<double>& b,
                                 size_t period) {
    size_t n = a.size();
    std::vector<double> out(n, NaN);
    if (n == 0 || n != b.size() || period == 0 || n < period) return out;
    for (size_t i = period - 1; i < n; ++i) {
        double sum_a = 0.0, sum_b = 0.0;
        for (size_t j = i + 1 - period; j <= i; ++j) {
            sum_a += a[j];
            sum_b += b[j];
        }
        double mean_a = sum_a / period;
        double mean_b = sum_b / period;
        double num = 0.0, den_a = 0.0, den_b = 0.0;
        for (size_t j = i + 1 - period; j <= i; ++j) {
            double da = a[j] - mean_a;
            double db = b[j] - mean_b;
            num += da * db;
            den_a += da * da;
            den_b += db * db;
        }
        double den = std::sqrt(den_a * den_b);
        if (den > 0.0) out[i] = num / den;
    }
    return out;
}

std::vector<double> shift(const std::vector<double>& vals, size_t n) {
    std::vector<double> out(vals.size(), NaN);
    if (n >= vals.size()) return out;
    for (size_t i = n; i < vals.size(); ++i) out[i] = vals[i - n];
    return out;
}

std::vector<double> pct_change(const std::vector<double>& vals, size_t n) {
    std::vector<double> out(vals.size(), NaN);
    if (n == 0 || vals.size() <= n) return out;
    for (size_t i = n; i < vals.size(); ++i) {
        if (vals[i - n] != 0.0) out[i] = vals[i] / vals[i - n] - 1.0;
    }
    return out;
}

std::vector<double> element_mul(const std::vector<double>& a, const std::vector<double>& b) {
    size_t n = std::min(a.size(), b.size());
    std::vector<double> out(n, NaN);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(a[i]) && !std::isnan(b[i])) out[i] = a[i] * b[i];
    }
    return out;
}

std::vector<double> element_div(const std::vector<double>& a, const std::vector<double>& b) {
    size_t n = std::min(a.size(), b.size());
    std::vector<double> out(n, NaN);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(a[i]) && !std::isnan(b[i]) && b[i] != 0.0) out[i] = a[i] / b[i];
    }
    return out;
}

std::vector<double> element_sub(const std::vector<double>& a, const std::vector<double>& b) {
    size_t n = std::min(a.size(), b.size());
    std::vector<double> out(n, NaN);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(a[i]) && !std::isnan(b[i])) out[i] = a[i] - b[i];
    }
    return out;
}

std::vector<double> scalar_sub(double s, const std::vector<double>& a) {
    std::vector<double> out(a.size(), NaN);
    for (size_t i = 0; i < a.size(); ++i) {
        if (!std::isnan(a[i])) out[i] = s - a[i];
    }
    return out;
}

std::vector<double> scalar_div(double s, const std::vector<double>& a) {
    std::vector<double> out(a.size(), NaN);
    for (size_t i = 0; i < a.size(); ++i) {
        if (!std::isnan(a[i]) && a[i] != 0.0) out[i] = s / a[i];
    }
    return out;
}

std::vector<double> bias_from_ma(const std::vector<double>& price,
                                 const std::vector<double>& ma) {
    size_t n = std::min(price.size(), ma.size());
    std::vector<double> out(n, NaN);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(ma[i]) && ma[i] > 0.0) out[i] = (price[i] - ma[i]) / ma[i];
    }
    return out;
}

} // namespace

std::map<std::string, std::vector<std::string>> feature_taxonomy() {
    return {
        {"price_volume", {
            "ret_1d", "ret_3d", "ret_5d", "ret_10d",
            "amplitude_5d", "amplitude_10d",
            "vol_ratio_5d", "vol_ratio_10d",
            "price_volume_corr_10d", "turnover_change_5d",
        }},
        {"momentum", {
            "momentum_5d", "momentum_10d", "momentum_20d", "momentum_60d",
            "momentum_slope_10d", "momentum_slope_20d",
            "momentum_accel_10d", "momentum_accel_20d",
        }},
        {"volatility", {
            "atr_norm_14", "hist_vol_10d", "hist_vol_20d", "hist_vol_60d",
            "vol_change_10d", "vol_change_20d",
        }},
        {"technical", {
            "rsi_14", "rsi_6", "adx_14",
            "macd_hist", "macd_signal", "macd_dif",
            "bbands_position", "kdj_k", "kdj_d",
            "cci_14", "willr_14", "obv_slope_10d",
        }},
        {"ma_pattern", {
            "ma5_bias", "ma10_bias", "ma20_bias", "ma60_bias",
            "ma_bull_score",
            "upper_shadow_ratio", "lower_shadow_ratio", "body_ratio",
            "new_high_20d", "new_low_20d",
        }},
        {"interaction", {
            "mom_vol_cross", "adx_rsi_cross",
            "vol_ratio_mom_cross", "rsi_bbands_cross",
            "macd_adx_cross", "vol_mom_accel_cross",
        }},
    };
}

std::vector<std::string> all_feature_names() {
    std::vector<std::string> out;
    for (const auto& kv : feature_taxonomy()) {
        out.insert(out.end(), kv.second.begin(), kv.second.end());
    }
    return out;
}

std::map<std::string, std::vector<double>> calc_features(
    const std::vector<quant::bt::Bar>& bars) {
    std::map<std::string, std::vector<double>> out;
    const size_t n = bars.size();
    if (n == 0) return out;

    std::vector<double> o(n), h(n), l(n), c(n), v(n);
    for (size_t i = 0; i < n; ++i) {
        o[i] = bars[i].open;
        h[i] = bars[i].high;
        l[i] = bars[i].low;
        c[i] = bars[i].close;
        v[i] = bars[i].volume;
    }

    // Price-volume
    out["ret_1d"] = pct_change(c, 1);
    out["ret_3d"] = pct_change(c, 3);
    out["ret_5d"] = pct_change(c, 5);
    out["ret_10d"] = pct_change(c, 10);

    auto high_max_5 = rolling_max(h, 5);
    auto low_min_5 = rolling_min(l, 5);
    auto close_mean_5 = rolling_mean(c, 5);
    std::vector<double> amp5(n, NaN);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(high_max_5[i]) && !std::isnan(low_min_5[i]) &&
            !std::isnan(close_mean_5[i]) && close_mean_5[i] != 0.0) {
            amp5[i] = (high_max_5[i] - low_min_5[i]) / close_mean_5[i];
        }
    }
    out["amplitude_5d"] = amp5;

    auto high_max_10 = rolling_max(h, 10);
    auto low_min_10 = rolling_min(l, 10);
    auto close_mean_10 = rolling_mean(c, 10);
    std::vector<double> amp10(n, NaN);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(high_max_10[i]) && !std::isnan(low_min_10[i]) &&
            !std::isnan(close_mean_10[i]) && close_mean_10[i] != 0.0) {
            amp10[i] = (high_max_10[i] - low_min_10[i]) / close_mean_10[i];
        }
    }
    out["amplitude_10d"] = amp10;

    auto vol_ma5 = rolling_mean(v, 5);
    auto vol_ma10 = rolling_mean(v, 10);
    auto vol_ma20 = rolling_mean(v, 20);
    out["vol_ratio_5d"] = element_div(v, vol_ma5);
    out["vol_ratio_10d"] = element_div(v, vol_ma10);
    out["price_volume_corr_10d"] = rolling_corr(c, v, 10);
    out["turnover_change_5d"] = element_div(vol_ma5, vol_ma20);

    // Momentum
    auto mom5 = quant::ind::roc(c, 5);
    auto mom10 = quant::ind::roc(c, 10);
    auto mom20 = quant::ind::roc(c, 20);
    auto mom60 = quant::ind::roc(c, 60);
    out["momentum_5d"] = mom5;
    out["momentum_10d"] = mom10;
    out["momentum_20d"] = mom20;
    out["momentum_60d"] = mom60;

    auto mom10_shift5 = shift(mom10, 5);
    auto mom20_shift10 = shift(mom20, 10);
    out["momentum_slope_10d"] = element_sub(mom10, mom10_shift5);
    out["momentum_slope_20d"] = element_sub(mom20, mom20_shift10);

    auto slope10 = out["momentum_slope_10d"];
    auto slope20 = out["momentum_slope_20d"];
    auto slope10_shift5 = shift(slope10, 5);
    auto slope20_shift10 = shift(slope20, 10);
    out["momentum_accel_10d"] = element_sub(slope10, slope10_shift5);
    out["momentum_accel_20d"] = element_sub(slope20, slope20_shift10);

    // Volatility
    auto atr14 = quant::ind::atr(h, l, c, 14);
    std::vector<double> atr_norm(n, NaN);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(atr14[i]) && c[i] > 0.0) atr_norm[i] = atr14[i] / c[i];
    }
    out["atr_norm_14"] = atr_norm;

    auto ret1d = out["ret_1d"];
    out["hist_vol_10d"] = rolling_std(ret1d, 10);
    out["hist_vol_20d"] = rolling_std(ret1d, 20);
    out["hist_vol_60d"] = rolling_std(ret1d, 60);
    for (auto& x : out["hist_vol_10d"]) if (!std::isnan(x)) x *= std::sqrt(252.0);
    for (auto& x : out["hist_vol_20d"]) if (!std::isnan(x)) x *= std::sqrt(252.0);
    for (auto& x : out["hist_vol_60d"]) if (!std::isnan(x)) x *= std::sqrt(252.0);

    auto hv10 = out["hist_vol_10d"];
    auto hv20 = out["hist_vol_20d"];
    out["vol_change_10d"] = element_sub(element_div(hv10, shift(hv10, 10)), std::vector<double>(n, 1.0));
    out["vol_change_20d"] = element_sub(element_div(hv20, shift(hv20, 20)), std::vector<double>(n, 1.0));

    // Technical
    out["rsi_14"] = quant::ind::rsi(c, 14);
    out["rsi_6"] = quant::ind::rsi(c, 6);
    auto adx14 = quant::ind::adx(h, l, c, 14);
    out["adx_14"] = adx14.adx;

    auto macd = quant::ind::macd(c, 12, 26, 9);
    out["macd_dif"] = macd.dif;
    out["macd_signal"] = macd.dea;
    out["macd_hist"] = macd.bar;

    auto bb = quant::ind::bollinger(c, 20, 2.0);
    std::vector<double> bb_pos(n, NaN);
    for (size_t i = 0; i < n; ++i) {
        double width = bb.upper[i] - bb.lower[i];
        if (!std::isnan(width) && width > 0.0) {
            bb_pos[i] = (c[i] - bb.lower[i]) / width;
        }
    }
    out["bbands_position"] = bb_pos;

    auto stoch = quant::ind::stoch(h, l, c, 9, 3, 3);
    out["kdj_k"] = stoch.k;
    out["kdj_d"] = stoch.d;

    out["cci_14"] = quant::ind::cci(h, l, c, 14);
    out["willr_14"] = quant::ind::willr(h, l, c, 14);

    auto obv_series = quant::ind::obv(c, v);
    auto obv_ma10 = rolling_mean(obv_series, 10);
    std::vector<double> obv_slope(n, NaN);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(obv_series[i]) && !std::isnan(obv_ma10[i]) && obv_ma10[i] != 0.0) {
            obv_slope[i] = (obv_series[i] - obv_ma10[i]) / std::abs(obv_ma10[i]);
        }
    }
    out["obv_slope_10d"] = obv_slope;

    // MA pattern
    auto ma5 = quant::ind::sma(c, 5);
    auto ma10 = quant::ind::sma(c, 10);
    auto ma20 = quant::ind::sma(c, 20);
    auto ma60 = quant::ind::sma(c, 60);
    out["ma5_bias"] = bias_from_ma(c, ma5);
    out["ma10_bias"] = bias_from_ma(c, ma10);
    out["ma20_bias"] = bias_from_ma(c, ma20);
    out["ma60_bias"] = bias_from_ma(c, ma60);

    std::vector<double> bull_score(n, NaN);
    for (size_t i = 0; i < n; ++i) {
        if (std::isnan(ma5[i]) || std::isnan(ma10[i]) || std::isnan(ma20[i]) || std::isnan(ma60[i]))
            continue;
        double score = 0.0;
        score += (c[i] > ma5[i]) ? 1.0 : 0.0;
        score += (c[i] > ma10[i]) ? 1.0 : 0.0;
        score += (c[i] > ma20[i]) ? 1.0 : 0.0;
        score += (c[i] > ma60[i]) ? 1.0 : 0.0;
        score += (ma5[i] > ma10[i]) ? 1.0 : 0.0;
        score += (ma10[i] > ma20[i]) ? 1.0 : 0.0;
        bull_score[i] = score / 6.0;
    }
    out["ma_bull_score"] = bull_score;

    std::vector<double> upper_shadow(n, NaN), lower_shadow(n, NaN), body_ratio(n, NaN);
    for (size_t i = 0; i < n; ++i) {
        double range = h[i] - l[i];
        if (range > 0.0) {
            upper_shadow[i] = (h[i] - std::max(c[i], o[i])) / range;
            lower_shadow[i] = (std::min(c[i], o[i]) - l[i]) / range;
            body_ratio[i] = std::abs(c[i] - o[i]) / range;
        }
    }
    out["upper_shadow_ratio"] = upper_shadow;
    out["lower_shadow_ratio"] = lower_shadow;
    out["body_ratio"] = body_ratio;

    auto high_20 = rolling_max(h, 20);
    auto low_20 = rolling_min(l, 20);
    std::vector<double> new_high(n, NaN), new_low(n, NaN);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(high_20[i])) new_high[i] = (h[i] >= high_20[i]) ? 1.0 : 0.0;
        if (!std::isnan(low_20[i])) new_low[i] = (l[i] <= low_20[i]) ? 1.0 : 0.0;
    }
    out["new_high_20d"] = new_high;
    out["new_low_20d"] = new_low;

    // Interaction
    out["mom_vol_cross"] = element_mul(out["momentum_20d"], out["atr_norm_14"]);
    {
        auto adx_rsi = element_mul(out["adx_14"], scalar_div(1.0, scalar_sub(50.0, out["rsi_14"])));
        // Original formula: adx * (rsi - 50) / 50
        for (size_t i = 0; i < n; ++i) {
            if (!std::isnan(out["rsi_14"][i]) && !std::isnan(out["adx_14"][i])) {
                adx_rsi[i] = out["adx_14"][i] * (out["rsi_14"][i] - 50.0) / 50.0;
            }
        }
        out["adx_rsi_cross"] = adx_rsi;
    }
    out["vol_ratio_mom_cross"] = element_mul(out["vol_ratio_5d"], out["momentum_10d"]);
    {
        auto rsi_bb = element_mul(scalar_div(1.0, scalar_sub(50.0, out["rsi_14"])), out["bbands_position"]);
        for (size_t i = 0; i < n; ++i) {
            if (!std::isnan(out["rsi_14"][i]) && !std::isnan(out["bbands_position"][i])) {
                rsi_bb[i] = (out["rsi_14"][i] - 50.0) / 50.0 * out["bbands_position"][i];
            }
        }
        out["rsi_bbands_cross"] = rsi_bb;
    }
    out["macd_adx_cross"] = element_mul(out["macd_hist"], out["adx_14"]);
    out["vol_mom_accel_cross"] = element_mul(out["hist_vol_10d"], out["momentum_accel_10d"]);

    return out;
}

std::map<std::string, std::vector<double>> calc_fundamental_features(
    const std::vector<quant::bt::Bar>& bars,
    const std::map<std::string, FinancialSeries>& fin_data) {
    std::map<std::string, std::vector<double>> out;
    const size_t n = bars.size();
    out["pe_ratio"].assign(n, NaN);
    out["roe_factor"].assign(n, NaN);
    out["gross_margin_factor"].assign(n, NaN);
    out["debt_ratio_factor"].assign(n, NaN);

    auto get_latest = [&](const FinancialSeries& series, const std::string& date) -> double {
        double value = NaN;
        for (const auto& kv : series) {
            if (kv.first <= date) value = kv.second;
            else break;
        }
        return value;
    };

    auto eps_it = fin_data.find("eps");
    auto roe_it = fin_data.find("roe");
    auto gm_it = fin_data.find("gross_margin");
    auto dr_it = fin_data.find("debt_ratio");

    for (size_t i = 0; i < n; ++i) {
        const std::string& d = bars[i].date;
        if (eps_it != fin_data.end()) {
            double eps = get_latest(eps_it->second, d);
            if (!std::isnan(eps) && eps != 0.0) {
                out["pe_ratio"][i] = bars[i].close / eps;
            }
        }
        if (roe_it != fin_data.end()) {
            out["roe_factor"][i] = get_latest(roe_it->second, d);
        }
        if (gm_it != fin_data.end()) {
            out["gross_margin_factor"][i] = get_latest(gm_it->second, d);
        }
        if (dr_it != fin_data.end()) {
            out["debt_ratio_factor"][i] = get_latest(dr_it->second, d);
        }
    }

    return out;
}

} // namespace quant::ml
