// 6-小市值轮动策略
// 对应 week5/10-网格与多因子-20260318/CASE-网格与多因子/6-小市值轮动策略.py

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

struct StockSnap {
    std::string code;
    double close = 0.0;
    double avg_volume_20d = 0.0;
    double momentum_20d = 0.0;
    double volatility = 0.0;
    double cap_proxy = 0.0;
    double total_assets = 0.0;
};

std::vector<StockSnap> build_snaps(const std::map<std::string, std::vector<bt::Bar>>& data,
                                   const std::map<std::string, double>& total_assets,
                                   const std::string& date) {
    std::vector<StockSnap> snaps;
    for (const auto& kv : data) {
        const auto& bars = kv.second;
        auto it = std::lower_bound(bars.begin(), bars.end(), date,
            [](const bt::Bar& b, const std::string& d) { return b.date < d; });
        if (it == bars.end() || it->date != date) continue;
        size_t idx = static_cast<size_t>(it - bars.begin());
        if (idx < 60) continue;

        std::vector<double> closes, volumes, highs, lows;
        for (size_t i = 0; i <= idx; ++i) {
            closes.push_back(bars[i].close);
            volumes.push_back(bars[i].volume);
            highs.push_back(bars[i].high);
            lows.push_back(bars[i].low);
        }
        auto vol_ma20 = ind::sma(volumes, 20);
        auto mom20 = ind::momentum(closes, 20);
        auto atr14 = ind::atr(highs, lows, closes, 14);
        if (std::isnan(vol_ma20[idx]) || std::isnan(mom20[idx]) || std::isnan(atr14[idx])) continue;

        StockSnap s;
        s.code = kv.first;
        s.close = it->close;
        s.avg_volume_20d = vol_ma20[idx];
        s.momentum_20d = mom20[idx] / 100.0;  // factor returns percent, convert to fraction
        s.volatility = atr14[idx] / it->close;
        s.cap_proxy = it->close * vol_ma20[idx];
        auto fa = total_assets.find(kv.first);
        if (fa != total_assets.end()) s.total_assets = fa->second;
        snaps.push_back(s);
    }
    return snaps;
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

PortResult run_pure_small_cap(const std::map<std::string, std::vector<bt::Bar>>& data,
                              const std::map<std::string, double>& total_assets,
                              const std::vector<std::string>& reb_dates,
                              size_t top_n) {
    PortResult res;
    res.name = fmt::format("纯小市值 Top-{}", top_n);
    double nav = 1.0, peak = 1.0, max_dd = 0.0;
    std::vector<double> rets;
    for (size_t i = 0; i + 1 < reb_dates.size(); ++i) {
        auto snaps = build_snaps(data, total_assets, reb_dates[i]);
        if (snaps.size() < top_n) continue;
        std::sort(snaps.begin(), snaps.end(),
                  [](const StockSnap& a, const StockSnap& b) { return a.cap_proxy < b.cap_proxy; });
        std::vector<std::string> holds;
        for (size_t k = 0; k < top_n && k < snaps.size(); ++k) holds.push_back(snaps[k].code);
        double r = 0.0;
        for (const auto& c : holds) r += forward_return(data.at(c), reb_dates[i], reb_dates[i + 1]);
        r /= holds.size();
        nav *= (1.0 + r);
        peak = std::max(peak, nav);
        max_dd = std::max(max_dd, 1.0 - nav / peak);
        rets.push_back(r);
        res.records.emplace_back(reb_dates[i], r, holds);
        ++res.rebalances;
    }
    res.total_return = nav - 1.0;
    res.max_drawdown = max_dd;
    if (!rets.empty()) {
        double mean = std::accumulate(rets.begin(), rets.end(), 0.0) / rets.size();
        double sq = 0.0;
        for (double v : rets) sq += (v - mean) * (v - mean);
        double stdv = std::sqrt(sq / rets.size());
        res.annual_return = std::pow(1.0 + mean, 12.0) - 1.0;
        res.sharpe = (stdv > 0.0) ? (mean * 12.0) / (stdv * std::sqrt(12.0)) : 0.0;
        int wins = std::count_if(rets.begin(), rets.end(), [](double v) { return v > 0.0; });
        res.monthly_win_rate = static_cast<double>(wins) / rets.size();
    }
    return res;
}

PortResult run_enhanced_small_cap(const std::map<std::string, std::vector<bt::Bar>>& data,
                                  const std::map<std::string, double>& total_assets,
                                  const std::vector<std::string>& reb_dates,
                                  size_t top_n,
                                  size_t pool_size = 50) {
    PortResult res;
    res.name = fmt::format("增强小市值 Top-{}", top_n);
    double nav = 1.0, peak = 1.0, max_dd = 0.0;
    std::vector<double> rets;
    for (size_t i = 0; i + 1 < reb_dates.size(); ++i) {
        auto snaps = build_snaps(data, total_assets, reb_dates[i]);
        if (snaps.size() < pool_size) continue;
        std::sort(snaps.begin(), snaps.end(),
                  [](const StockSnap& a, const StockSnap& b) { return a.cap_proxy < b.cap_proxy; });
        std::vector<StockSnap> pool(snaps.begin(), snaps.begin() + std::min(pool_size, snaps.size()));

        // Rank cap within pool (smaller better).
        std::vector<std::optional<double>> caps, moms, vols;
        for (const auto& s : pool) {
            caps.emplace_back(s.cap_proxy);
            moms.emplace_back(s.momentum_20d);
            vols.emplace_back(s.volatility);
        }
        auto cap_rank = factor::rank_pct(caps, false);  // smaller better
        auto mom_rank = factor::rank_pct(moms, true);
        auto vol_rank = factor::rank_pct(vols, false);

        struct Scored { std::string code; double score; };
        std::vector<Scored> scored;
        for (size_t k = 0; k < pool.size(); ++k) {
            if (pool[k].momentum_20d < -0.20 || pool[k].volatility > 0.05) continue;
            double mom_comp = std::min(pool[k].momentum_20d, 0.30) / 0.30;
            double vol_comp = 1.0 - std::min(pool[k].volatility, 0.05) / 0.05;
            double score = cap_rank[k] * 0.5 + mom_comp * 0.3 + vol_comp * 0.2;
            scored.push_back({pool[k].code, score});
        }
        if (scored.empty()) continue;
        std::sort(scored.begin(), scored.end(),
                  [](const Scored& a, const Scored& b) { return a.score > b.score; });
        std::vector<std::string> holds;
        for (size_t k = 0; k < top_n && k < scored.size(); ++k) holds.push_back(scored[k].code);
        double r = 0.0;
        for (const auto& c : holds) r += forward_return(data.at(c), reb_dates[i], reb_dates[i + 1]);
        r /= holds.size();
        nav *= (1.0 + r);
        peak = std::max(peak, nav);
        max_dd = std::max(max_dd, 1.0 - nav / peak);
        rets.push_back(r);
        res.records.emplace_back(reb_dates[i], r, holds);
        ++res.rebalances;
    }
    res.total_return = nav - 1.0;
    res.max_drawdown = max_dd;
    if (!rets.empty()) {
        double mean = std::accumulate(rets.begin(), rets.end(), 0.0) / rets.size();
        double sq = 0.0;
        for (double v : rets) sq += (v - mean) * (v - mean);
        double stdv = std::sqrt(sq / rets.size());
        res.annual_return = std::pow(1.0 + mean, 12.0) - 1.0;
        res.sharpe = (stdv > 0.0) ? (mean * 12.0) / (stdv * std::sqrt(12.0)) : 0.0;
        int wins = std::count_if(rets.begin(), rets.end(), [](double v) { return v > 0.0; });
        res.monthly_win_rate = static_cast<double>(wins) / rets.size();
    }
    return res;
}

void print_result(const PortResult& r) {
    fmt::print("\n[{}]\n", r.name);
    fmt::print("  总收益: {:+.2f}% | 年化: {:+.2f}% | 最大回撤: {:.2f}%\n",
               r.total_return * 100.0, r.annual_return * 100.0, r.max_drawdown * 100.0);
    fmt::print("  夏普: {:.2f} | 月度胜率: {:.1f}% | 调仓期数: {}\n",
               r.sharpe, r.monthly_win_rate * 100.0, r.rebalances);
    fmt::print("  最近3期调仓:\n");
    size_t start = r.records.size() > 3 ? r.records.size() - 3 : 0;
    for (size_t i = start; i < r.records.size(); ++i) {
        const auto& [date, ret, holds] = r.records[i];
        fmt::print("    {} {:+.2f}% 持仓: ", date, ret * 100.0);
        for (size_t j = 0; j < holds.size(); ++j) {
            if (j) fmt::print(",");
            fmt::print("{}", holds[j]);
        }
        fmt::print("\n");
    }
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    const std::string start_date = "2024-01-01";
    const std::string end_date = "2025-12-31";
    const int min_bars = 60;

    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("小市值轮动策略\n");
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

    auto total_assets = bt::data::load_latest_total_assets(cfg, end_date);
    fmt::print("  读取财务 total_assets: {} 只\n", total_assets.size());

    auto pure = run_pure_small_cap(data, total_assets, reb_dates, 10);
    print_result(pure);

    fmt::print("\n--- 增强版小市值 (Top-N 对比) ---\n");
    std::vector<size_t> top_ns = {5, 10, 20};
    fmt::print("  {:<6} {:>10} {:>10} {:>10} {:>8} {:>10} {:>10}\n",
               "Top-N", "总收益", "年化", "最大回撤", "夏普", "月度胜率", "调仓数");
    for (size_t n : top_ns) {
        auto r = run_enhanced_small_cap(data, total_assets, reb_dates, n, 50);
        fmt::print("  {:<6} {:>+9.2f}% {:>+9.2f}% {:>9.2f}% {:>8.2f} {:>9.1f}% {:>10d}\n",
                   n, r.total_return * 100.0, r.annual_return * 100.0,
                   r.max_drawdown * 100.0, r.sharpe, r.monthly_win_rate * 100.0,
                   r.rebalances);
    }

    return 0;
}
