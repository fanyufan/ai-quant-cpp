// 8-ML增强多因子
// 对应 week5/10-网格与多因子-20260318/CASE-网格与多因子/8-ML增强多因子.py

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "backtest.hpp"
#include "backtest_data_mysql.hpp"
#include "env.hpp"
#include "factor_engine.hpp"
#include "indicators.hpp"
#include "ml_tree.hpp"

using namespace quant;

namespace {

std::vector<std::string> month_end_dates(const std::vector<std::string>& all_dates) {
    std::vector<std::string> ends;
    if (all_dates.empty()) return ends;
    for (size_t i = 1; i < all_dates.size(); ++i) {
        if (all_dates[i].substr(0, 7) != all_dates[i - 1].substr(0, 7)) ends.push_back(all_dates[i - 1]);
    }
    ends.push_back(all_dates.back());
    ends.erase(std::unique(ends.begin(), ends.end()), ends.end());
    return ends;
}

double forward_return(const std::vector<bt::Bar>& bars, const std::string& start, const std::string& end) {
    auto it_s = std::lower_bound(bars.begin(), bars.end(), start,
        [](const bt::Bar& b, const std::string& d) { return b.date < d; });
    auto it_e = std::lower_bound(bars.begin(), bars.end(), end,
        [](const bt::Bar& b, const std::string& d) { return b.date < d; });
    if (it_s == bars.end() || it_s->date != start) return 0.0;
    if (it_e == bars.begin()) return 0.0;
    --it_e;
    if (it_e->date < start) return 0.0;
    return it_e->close / it_s->close - 1.0;
}

struct StockFeat {
    std::string code;
    std::vector<double> x;
    double target = 0.0;
    double momentum_20d = 0.0;
};

StockFeat extract_features(const std::vector<bt::Bar>& bars,
                           const std::string& date,
                           const std::string& next_date) {
    StockFeat sf;
    auto it = std::lower_bound(bars.begin(), bars.end(), date,
        [](const bt::Bar& b, const std::string& d) { return b.date < d; });
    if (it == bars.end() || it->date != date) return sf;
    size_t idx = static_cast<size_t>(it - bars.begin());
    if (idx < 60 || idx + 20 >= bars.size()) return sf;

    std::vector<double> closes, highs, lows, volumes;
    for (size_t i = 0; i <= idx; ++i) {
        closes.push_back(bars[i].close);
        highs.push_back(bars[i].high);
        lows.push_back(bars[i].low);
        volumes.push_back(bars[i].volume);
    }
    auto mom5 = ind::momentum(closes, 5);
    auto mom10 = ind::momentum(closes, 10);
    auto mom60 = ind::momentum(closes, 60);
    auto atr14 = ind::atr(highs, lows, closes, 14);
    auto rsi14 = ind::rsi(closes, 14);
    auto adx14 = ind::adx(highs, lows, closes, 14);
    auto macd = ind::macd(closes, 12, 26, 9);
    auto vol_ma20 = ind::sma(volumes, 20);
    auto obv_series = factor::obv(std::vector<bt::Bar>(bars.begin(), it + 1));
    auto obv_ma5 = ind::sma(obv_series, 5);

    // Use the 8-factor engine for momentum_20d, volatility, rsi, adx, macd_signal,
    // turnover_ratio, price_position.
    std::vector<bt::Bar> slice(bars.begin(), it + 1);
    auto fac = factor::calc_all_factors(slice);
    auto getf = [&fac](const std::string& name) -> double {
        auto itf = fac.find(name);
        return (itf != fac.end()) ? itf->second : std::numeric_limits<double>::quiet_NaN();
    };
    double mom20v = getf("momentum_20d") / 100.0;
    double volat = getf("volatility");
    double rsi = getf("rsi_14");
    double adx = getf("adx_14");
    double macd_hist = getf("macd_signal");
    double vol_ratio = getf("turnover_ratio");
    double price_pos = getf("price_position");

    if (std::isnan(mom5[idx]) || std::isnan(mom10[idx]) || std::isnan(mom20v) ||
        std::isnan(mom60[idx]) || std::isnan(volat) || std::isnan(rsi) ||
        std::isnan(adx) || std::isnan(macd_hist) || std::isnan(vol_ratio) ||
        std::isnan(price_pos) || std::isnan(obv_ma5[idx])) {
        return sf;
    }

    double mom5v = mom5[idx] / 100.0;
    double mom10v = mom10[idx] / 100.0;
    double mom60v = mom60[idx] / 100.0;
    double obv_slope = (obv_series[idx] - obv_ma5[idx]) / (obv_ma5[idx] + 1e-10);
    double mom_vol_cross = mom20v * volat;
    double adx_rsi_cross = adx * (rsi - 50.0) / 100.0;

    sf.code = bars.front().date; // placeholder, caller sets code
    sf.x = {mom5v, mom10v, mom20v, mom60v, volat, rsi, adx, macd_hist,
            obv_slope, vol_ratio, price_pos, mom_vol_cross, adx_rsi_cross};
    sf.target = forward_return(bars, date, next_date);
    sf.momentum_20d = mom20v;
    return sf;
}

struct PortResult {
    std::string name;
    double total_return = 0.0;
    double annual_return = 0.0;
    double max_drawdown = 0.0;
    double sharpe = 0.0;
    double monthly_win_rate = 0.0;
    size_t rebalances = 0;
    std::vector<std::tuple<std::string, double, std::vector<std::string>>> records;
};

void compute_metrics(PortResult& r, const std::vector<double>& rets) {
    if (rets.empty()) return;
    double nav = 1.0, peak = 1.0;
    for (double v : rets) {
        nav *= (1.0 + v);
        peak = std::max(peak, nav);
        r.max_drawdown = std::max(r.max_drawdown, 1.0 - nav / peak);
    }
    r.total_return = nav - 1.0;
    double mean = std::accumulate(rets.begin(), rets.end(), 0.0) / rets.size();
    double sq = 0.0;
    for (double v : rets) sq += (v - mean) * (v - mean);
    double stdv = std::sqrt(sq / rets.size());
    r.annual_return = std::pow(1.0 + mean, 12.0) - 1.0;
    r.sharpe = (stdv > 0.0) ? (mean * 12.0) / (stdv * std::sqrt(12.0)) : 0.0;
    int wins = std::count_if(rets.begin(), rets.end(), [](double v) { return v > 0.0; });
    r.monthly_win_rate = static_cast<double>(wins) / rets.size();
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    const std::string start_date = "2023-01-01";
    const std::string end_date = "2025-12-31";
    const int min_bars = 80;
    const size_t train_months = 12;
    const size_t top_n = 10;

    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("ML增强多因子选股\n");
    fmt::print("{0}\n", std::string(70, '='));

    auto all_data = bt::data::load_all_from_mysql(cfg);
    std::map<std::string, std::vector<bt::Bar>> data;
    for (auto& kv : all_data) {
        std::vector<bt::Bar> filtered;
        for (const auto& b : kv.second) {
            if (b.date >= start_date && b.date <= end_date) filtered.push_back(b);
        }
        if (static_cast<int>(filtered.size()) >= min_bars) {
            data[kv.first] = std::move(filtered);
        }
    }

    std::set<std::string> date_set;
    for (const auto& kv : data) {
        for (const auto& b : kv.second) date_set.insert(b.date);
    }
    std::vector<std::string> all_dates(date_set.begin(), date_set.end());
    auto reb_dates = month_end_dates(all_dates);
    fmt::print("  有效股票数: {} | 月末日期数: {}\n", data.size(), reb_dates.size());

    if (reb_dates.size() <= train_months + 1) {
        fmt::print("  可回测期数不足\n");
        return 1;
    }

    std::vector<std::string> feature_names = {
        "momentum_5d", "momentum_10d", "momentum_20d", "momentum_60d",
        "volatility", "rsi_14", "adx_14", "macd_hist",
        "obv_slope", "vol_ratio", "price_position", "mom_vol_cross", "adx_rsi_cross"};

    PortResult ml_result;
    ml_result.name = "ML增强多因子 Top-10";
    std::vector<double> ml_rets;

    PortResult bench_result;
    bench_result.name = "动量排序基准 Top-10";
    std::vector<double> bench_rets;

    std::vector<double> last_importances;

    for (size_t i = train_months; i + 1 < reb_dates.size(); ++i) {
        const std::string& test_date = reb_dates[i];
        const std::string& next_date = reb_dates[i + 1];

        // Training samples from previous train_months rebalance points.
        std::vector<std::vector<double>> X;
        std::vector<double> y;
        for (size_t t = i - train_months; t < i; ++t) {
            const std::string& tr_date = reb_dates[t];
            const std::string& tr_next = reb_dates[t + 1];
            for (const auto& kv : data) {
                auto sf = extract_features(kv.second, tr_date, tr_next);
                if (sf.x.empty()) continue;
                sf.code = kv.first;
                X.push_back(sf.x);
                y.push_back(sf.target);
            }
        }
        if (X.size() < 20) continue;

        ml::DecisionTree model(true, 5, 10);
        model.fit(X, y);
        last_importances = model.feature_importances();

        // Predict for test_date.
        std::vector<StockFeat> test_feats;
        for (const auto& kv : data) {
            auto sf = extract_features(kv.second, test_date, next_date);
            if (sf.x.empty()) continue;
            sf.code = kv.first;
            test_feats.push_back(sf);
        }
        for (auto& sf : test_feats) {
            sf.target = model.predict(sf.x);  // predicted return
        }
        std::sort(test_feats.begin(), test_feats.end(),
                  [](const StockFeat& a, const StockFeat& b) { return a.target > b.target; });
        std::vector<std::string> ml_holds;
        double ml_ret = 0.0;
        for (size_t k = 0; k < top_n && k < test_feats.size(); ++k) {
            ml_holds.push_back(test_feats[k].code);
            ml_ret += test_feats[k].x[2]; // actual momentum_20d as rough? better real return
        }
        // Compute actual portfolio return.
        ml_ret = 0.0;
        for (const auto& code : ml_holds) {
            ml_ret += forward_return(data.at(code), test_date, next_date);
        }
        if (!ml_holds.empty()) ml_ret /= ml_holds.size();
        ml_rets.push_back(ml_ret);
        ml_result.records.emplace_back(test_date, ml_ret, ml_holds);
        ++ml_result.rebalances;

        // Benchmark momentum sort.
        std::sort(test_feats.begin(), test_feats.end(),
                  [](const StockFeat& a, const StockFeat& b) { return a.momentum_20d > b.momentum_20d; });
        std::vector<std::string> bench_holds;
        for (size_t k = 0; k < top_n && k < test_feats.size(); ++k) {
            bench_holds.push_back(test_feats[k].code);
        }
        double bench_ret = 0.0;
        for (const auto& code : bench_holds) {
            bench_ret += forward_return(data.at(code), test_date, next_date);
        }
        if (!bench_holds.empty()) bench_ret /= bench_holds.size();
        bench_rets.push_back(bench_ret);
        bench_result.records.emplace_back(test_date, bench_ret, bench_holds);
        ++bench_result.rebalances;
    }

    compute_metrics(ml_result, ml_rets);
    compute_metrics(bench_result, bench_rets);

    fmt::print("\n样本量统计:\n");
    fmt::print("  训练窗口: {}个月 | 回测期数: {}\n", train_months, ml_result.rebalances);

    fmt::print("\nML模型特征重要性 (最近一期):\n");
    for (size_t i = 0; i < feature_names.size() && i < last_importances.size(); ++i) {
        std::string bar(static_cast<size_t>(last_importances[i] * 25), '#');
        fmt::print("  {:<18} {:.2f} {}\n", feature_names[i], last_importances[i], bar);
    }

    fmt::print("\n绩效对比:\n");
    fmt::print("  {:<20} {:>10} {:>10} {:>10} {:>8} {:>10} {:>10}\n",
               "策略", "总收益", "年化", "最大回撤", "夏普", "月度胜率", "调仓数");
    auto print = [](const PortResult& r) {
        fmt::print("  {:<20} {:>+9.2f}% {:>+9.2f}% {:>9.2f}% {:>8.2f} {:>9.1f}% {:>10d}\n",
                   r.name, r.total_return * 100.0, r.annual_return * 100.0,
                   r.max_drawdown * 100.0, r.sharpe, r.monthly_win_rate * 100.0,
                   r.rebalances);
    };
    print(ml_result);
    print(bench_result);

    fmt::print("\n最近3期ML选股记录:\n");
    size_t start = ml_result.records.size() > 3 ? ml_result.records.size() - 3 : 0;
    for (size_t i = start; i < ml_result.records.size(); ++i) {
        const auto& [date, ret, holds] = ml_result.records[i];
        fmt::print("  {} {:+.2f}% 持仓: ", date, ret * 100.0);
        for (size_t j = 0; j < holds.size(); ++j) {
            if (j) fmt::print(",");
            fmt::print("{}", holds[j]);
        }
        fmt::print("\n");
    }

    return 0;
}
