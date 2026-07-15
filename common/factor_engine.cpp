#include "factor_engine.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <fmt/format.h>
#include <limits>

#include "indicators.hpp"

namespace quant::factor {

std::vector<FactorConfig> default_factor_config() {
    return {
        {"momentum_20d", "20日动量", 1, 0.20},
        {"momentum_60d", "60日动量", 1, 0.15},
        {"volatility", "波动率", -1, 0.15},
        {"rsi_14", "RSI(14)", -1, 0.10},
        {"adx_14", "ADX(14)", 1, 0.10},
        {"turnover_ratio", "换手率指标", 1, 0.10},
        {"price_position", "价格位置", -1, 0.10},
        {"macd_signal", "MACD信号", 1, 0.10},
    };
}

namespace {

double last_valid(const std::vector<double>& v) {
    for (size_t i = v.size(); i-- > 0;) {
        if (!std::isnan(v[i])) return v[i];
    }
    return std::numeric_limits<double>::quiet_NaN();
}

std::vector<double> rolling_max(const std::vector<double>& vals, size_t period) {
    std::vector<double> out(vals.size(), std::numeric_limits<double>::quiet_NaN());
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
    std::vector<double> out(vals.size(), std::numeric_limits<double>::quiet_NaN());
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

} // namespace

std::map<std::string, double> calc_all_factors(const std::vector<quant::bt::Bar>& bars) {
    std::map<std::string, double> out;
    if (bars.size() < 60) return out;

    std::vector<double> opens, highs, lows, closes, volumes;
    opens.reserve(bars.size());
    highs.reserve(bars.size());
    lows.reserve(bars.size());
    closes.reserve(bars.size());
    volumes.reserve(bars.size());
    for (const auto& b : bars) {
        opens.push_back(b.open);
        highs.push_back(b.high);
        lows.push_back(b.low);
        closes.push_back(b.close);
        volumes.push_back(b.volume);
    }

    auto mom20 = quant::ind::momentum(closes, 20);
    auto mom60 = quant::ind::momentum(closes, 60);
    auto atr14 = quant::ind::atr(highs, lows, closes, 14);
    auto rsi14 = quant::ind::rsi(closes, 14);
    auto adx14 = quant::ind::adx(highs, lows, closes, 14);
    auto vol_ma20 = quant::ind::sma(volumes, 20);
    auto max60 = rolling_max(closes, 60);
    auto min60 = rolling_min(closes, 60);
    auto macd = quant::ind::macd(closes, 12, 26, 9);

    double close = closes.back();
    out["momentum_20d"] = last_valid(mom20);
    out["momentum_60d"] = last_valid(mom60);
    double atrv = last_valid(atr14);
    out["volatility"] = (!std::isnan(atrv) && close != 0.0) ? atrv / close : std::numeric_limits<double>::quiet_NaN();
    out["rsi_14"] = last_valid(rsi14);
    out["adx_14"] = last_valid(adx14.adx);
    double vol = volumes.back();
    double vma = last_valid(vol_ma20);
    out["turnover_ratio"] = (!std::isnan(vma) && vma > 0.0) ? vol / vma : std::numeric_limits<double>::quiet_NaN();
    double hh = last_valid(max60);
    double ll = last_valid(min60);
    out["price_position"] = (!std::isnan(hh) && !std::isnan(ll) && hh > ll) ?
                            (close - ll) / (hh - ll) : std::numeric_limits<double>::quiet_NaN();
    out["macd_signal"] = last_valid(macd.bar);
    return out;
}

std::map<std::string, std::map<std::string, double>> batch_calc_factors(
    const std::map<std::string, std::vector<quant::bt::Bar>>& all_data) {
    std::map<std::string, std::map<std::string, double>> out;
    for (const auto& kv : all_data) {
        out[kv.first] = calc_all_factors(kv.second);
    }
    return out;
}

std::vector<double> obv(const std::vector<quant::bt::Bar>& bars) {
    std::vector<double> out;
    out.reserve(bars.size());
    double obv_val = 0.0;
    for (size_t i = 0; i < bars.size(); ++i) {
        if (i == 0) {
            obv_val = bars[i].volume;
        } else {
            if (bars[i].close > bars[i - 1].close) obv_val += bars[i].volume;
            else if (bars[i].close < bars[i - 1].close) obv_val -= bars[i].volume;
        }
        out.push_back(obv_val);
    }
    return out;
}

std::vector<double> rank_pct(const std::vector<std::optional<double>>& values,
                             bool higher_better) {
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> out(values.size(), NaN);
    struct Item { double value; size_t idx; };
    std::vector<Item> items;
    items.reserve(values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i].has_value() && !std::isnan(*values[i])) {
            items.push_back({*values[i], i});
        }
    }
    size_t n = items.size();
    if (n < 2) return out;
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.value < b.value; });
    size_t pos = 0;
    while (pos < n) {
        size_t end = pos;
        while (end < n && items[end].value == items[pos].value) ++end;
        double avg_rank = (static_cast<double>(pos) + 1.0 + static_cast<double>(end)) / 2.0;
        double pct = (avg_rank - 1.0) / (static_cast<double>(n) - 1.0);
        if (!higher_better) pct = 1.0 - pct;
        for (size_t k = pos; k < end; ++k) {
            out[items[k].idx] = pct;
        }
        pos = end;
    }
    return out;
}

std::vector<ScoredStock> score_stocks(
    const std::map<std::string, std::map<std::string, double>>& factor_data,
    const std::vector<FactorConfig>& config) {
    std::vector<ScoredStock> out;
    out.reserve(factor_data.size());
    for (const auto& kv : factor_data) {
        ScoredStock s;
        s.code = kv.first;
        s.values = kv.second;
        out.push_back(std::move(s));
    }

    for (const auto& fc : config) {
        std::vector<std::optional<double>> vals;
        vals.reserve(out.size());
        for (const auto& s : out) {
            auto it = s.values.find(fc.name);
            if (it != s.values.end() && !std::isnan(it->second)) {
                vals.emplace_back(it->second);
            } else {
                vals.emplace_back();
            }
        }
        auto rp = rank_pct(vals, fc.direction == 1);
        for (size_t i = 0; i < out.size(); ++i) {
            out[i].ranks[fc.name] = rp[i];
            if (!std::isnan(rp[i])) {
                out[i].score += rp[i] * fc.weight;
            }
        }
    }

    std::sort(out.begin(), out.end(),
              [](const ScoredStock& a, const ScoredStock& b) {
                  if (std::isnan(a.score) && std::isnan(b.score)) return a.code < b.code;
                  if (std::isnan(a.score)) return false;
                  if (std::isnan(b.score)) return true;
                  return a.score > b.score;
              });
    return out;
}

std::vector<std::string> select_top_stocks(
    const std::map<std::string, std::map<std::string, double>>& factor_data,
    size_t top_n,
    const std::vector<FactorConfig>& config) {
    auto scored = score_stocks(factor_data, config);
    std::vector<std::string> out;
    out.reserve(std::min(top_n, scored.size()));
    for (size_t i = 0; i < scored.size() && i < top_n; ++i) {
        out.push_back(scored[i].code);
    }
    return out;
}

void print_factor_report(const std::vector<ScoredStock>& scored,
                         size_t top_n,
                         const std::string& title) {
    if (!title.empty()) fmt::print("\n{}\n", title);
    if (scored.empty()) {
        fmt::print("  (无有效打分数据)\n");
        return;
    }
    fmt::print("  排名 代码        综合得分");
    if (!scored.front().ranks.empty()) {
        for (const auto& kv : scored.front().ranks) {
            fmt::print(" {:>12}", kv.first);
        }
    }
    fmt::print("\n");
    size_t n = std::min(top_n, scored.size());
    for (size_t i = 0; i < n; ++i) {
        fmt::print("  {:>3}  {:<10} {:>8.4f}", i + 1, scored[i].code, scored[i].score);
        for (const auto& kv : scored[i].ranks) {
            fmt::print(" {:>12.2f}", kv.second);
        }
        fmt::print("\n");
    }
}

} // namespace quant::factor
