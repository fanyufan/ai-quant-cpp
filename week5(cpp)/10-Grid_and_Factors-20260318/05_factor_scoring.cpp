// 5-多因子打分选股
// 对应 week5/10-网格与多因子-20260318/CASE-网格与多因子/5-多因子打分选股.py

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

using namespace quant;

namespace {

std::vector<std::string> month_end_dates(const std::vector<std::string>& all_dates) {
    std::vector<std::string> ends;
    if (all_dates.empty()) return ends;
    for (size_t i = 1; i < all_dates.size(); ++i) {
        const std::string& prev = all_dates[i - 1];
        const std::string& cur = all_dates[i];
        if (cur.substr(0, 7) != prev.substr(0, 7)) ends.push_back(prev);
    }
    ends.push_back(all_dates.back());
    ends.erase(std::unique(ends.begin(), ends.end()), ends.end());
    return ends;
}

std::map<std::string, double> get_factor_values(
    const std::map<std::string, std::vector<bt::Bar>>& data,
    const std::string& date) {
    std::map<std::string, double> values; // code -> score
    for (const auto& kv : data) {
        const auto& bars = kv.second;
        auto it = std::lower_bound(bars.begin(), bars.end(), date,
            [](const bt::Bar& b, const std::string& d) { return b.date < d; });
        if (it == bars.end() || it->date != date) continue;
        std::vector<bt::Bar> slice(bars.begin(), it + 1);
        auto fac = factor::calc_all_factors(slice);
        if (!fac.empty()) {
            auto scored = factor::score_stocks({{kv.first, fac}});
            if (!scored.empty()) values[kv.first] = scored.front().score;
        }
    }
    return values;
}

double forward_return(const std::vector<bt::Bar>& bars, const std::string& start, const std::string& end) {
    auto it_s = std::lower_bound(bars.begin(), bars.end(), start,
        [](const bt::Bar& b, const std::string& d) { return b.date < d; });
    auto it_e = std::lower_bound(bars.begin(), bars.end(), end,
        [](const bt::Bar& b, const std::string& d) { return b.date < d; });
    if (it_s == bars.end() || it_e == bars.end()) return 0.0;
    if (it_s->date != start) return 0.0;
    if (it_e == bars.begin()) return 0.0;
    --it_e;  // last bar before/at end
    if (it_e->date < start) return 0.0;
    return it_e->close / it_s->close - 1.0;
}

struct PortResult {
    size_t top_n = 0;
    double total_return = 0.0;
    double annual_return = 0.0;
    double max_drawdown = 0.0;
    double sharpe = 0.0;
    double monthly_win_rate = 0.0;
    size_t rebalances = 0;
    std::vector<std::tuple<std::string, double, std::vector<std::string>>> records;
};

PortResult run_portfolio(const std::map<std::string, std::vector<bt::Bar>>& data,
                         const std::vector<std::string>& reb_dates,
                         size_t top_n) {
    PortResult res;
    res.top_n = top_n;
    double nav = 1.0;
    double peak = 1.0;
    double max_dd = 0.0;
    std::vector<double> monthly_rets;
    for (size_t i = 0; i + 1 < reb_dates.size(); ++i) {
        const std::string& rd = reb_dates[i];
        const std::string& next_rd = reb_dates[i + 1];

        std::map<std::string, std::map<std::string, double>> factor_data;
        for (const auto& kv : data) {
            const auto& bars = kv.second;
            auto it = std::lower_bound(bars.begin(), bars.end(), rd,
                [](const bt::Bar& b, const std::string& d) { return b.date < d; });
            if (it == bars.end() || it->date != rd) continue;
            std::vector<bt::Bar> slice(bars.begin(), it + 1);
            factor_data[kv.first] = factor::calc_all_factors(slice);
        }
        auto selected = factor::select_top_stocks(factor_data, top_n);
        if (selected.empty()) continue;

        double port_ret = 0.0;
        int cnt = 0;
        for (const auto& code : selected) {
            auto it = data.find(code);
            if (it == data.end()) continue;
            double r = forward_return(it->second, rd, next_rd);
            port_ret += r;
            ++cnt;
        }
        if (cnt > 0) port_ret /= cnt;

        nav *= (1.0 + port_ret);
        peak = std::max(peak, nav);
        max_dd = std::max(max_dd, 1.0 - nav / peak);
        monthly_rets.push_back(port_ret);
        res.records.emplace_back(rd, port_ret, selected);
        ++res.rebalances;
    }

    res.total_return = nav - 1.0;
    res.max_drawdown = max_dd;
    if (!monthly_rets.empty()) {
        double mean_ret = std::accumulate(monthly_rets.begin(), monthly_rets.end(), 0.0) /
                          monthly_rets.size();
        double sq = 0.0;
        for (double r : monthly_rets) sq += (r - mean_ret) * (r - mean_ret);
        double std_ret = std::sqrt(sq / monthly_rets.size());
        res.annual_return = std::pow(1.0 + mean_ret, 12.0) - 1.0;
        res.sharpe = (std_ret > 0.0) ? (mean_ret * 12.0) / (std_ret * std::sqrt(12.0)) : 0.0;
        int wins = std::count_if(monthly_rets.begin(), monthly_rets.end(),
                                 [](double r) { return r > 0.0; });
        res.monthly_win_rate = static_cast<double>(wins) / monthly_rets.size();
    }
    return res;
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    const std::string start_date = "2024-01-01";
    const std::string end_date = "2025-12-31";
    const int min_bars = 120;

    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("多因子打分选股\n");
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
    fmt::print("  有效股票数: {} | 调仓期数: {}\n", data.size(), reb_dates.size());

    // Latest cross-sectional ranking.
    const std::string& latest = reb_dates.empty() ? "" : reb_dates.back();
    if (!latest.empty()) {
        std::map<std::string, std::map<std::string, double>> factor_data;
        for (const auto& kv : data) {
            const auto& bars = kv.second;
            auto it = std::lower_bound(bars.begin(), bars.end(), latest,
                [](const bt::Bar& b, const std::string& d) { return b.date < d; });
            if (it == bars.end() || it->date != latest) continue;
            std::vector<bt::Bar> slice(bars.begin(), it + 1);
            factor_data[kv.first] = factor::calc_all_factors(slice);
        }
        auto scored = factor::score_stocks(factor_data);
        fmt::print("\n最新交易日 {} 截面打分 Top-10:\n", latest);
        factor::print_factor_report(scored, 10, "");
    }

    std::vector<size_t> top_ns = {5, 10, 20};
    std::vector<PortResult> results;
    for (size_t n : top_ns) {
        results.push_back(run_portfolio(data, reb_dates, n));
    }

    fmt::print("\nTop-N 策略绩效:\n");
    fmt::print("  {:<6} {:>10} {:>10} {:>10} {:>8} {:>10} {:>10}\n",
               "Top-N", "总收益", "年化", "最大回撤", "夏普", "月度胜率", "调仓数");
    for (const auto& r : results) {
        fmt::print("  {:<6} {:>+9.2f}% {:>+9.2f}% {:>9.2f}% {:>8.2f} {:>9.1f}% {:>10d}\n",
                   r.top_n, r.total_return * 100.0, r.annual_return * 100.0,
                   r.max_drawdown * 100.0, r.sharpe, r.monthly_win_rate * 100.0,
                   r.rebalances);
    }

    fmt::print("\n前3期调仓记录 (Top-10):\n");
    auto it10 = std::find_if(results.begin(), results.end(),
                             [](const PortResult& r) { return r.top_n == 10; });
    if (it10 != results.end()) {
        for (size_t i = 0; i < std::min<size_t>(3, it10->records.size()); ++i) {
            const auto& [date, ret, holds] = it10->records[i];
            fmt::print("  {} 收益: {:+.2f}% 持仓: ", date, ret * 100.0);
            for (size_t j = 0; j < holds.size(); ++j) {
                if (j) fmt::print(",");
                fmt::print("{}", holds[j]);
            }
            fmt::print("\n");
        }
    }

    return 0;
}
