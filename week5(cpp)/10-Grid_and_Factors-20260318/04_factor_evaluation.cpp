// 4-多因子评价框架
// 对应 week5/10-网格与多因子-20260318/CASE-网格与多因子/4-多因子评价框架.py

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
        if (cur.size() >= 7 && prev.size() >= 7 && cur.substr(0, 7) != prev.substr(0, 7)) {
            ends.push_back(prev);
        }
    }
    ends.push_back(all_dates.back());
    // Unique & sorted.
    ends.erase(std::unique(ends.begin(), ends.end()), ends.end());
    return ends;
}

double spearman(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size() || x.size() < 3) return std::numeric_limits<double>::quiet_NaN();
    std::vector<std::optional<double>> ox, oy;
    ox.reserve(x.size()); oy.reserve(y.size());
    for (size_t i = 0; i < x.size(); ++i) {
        if (std::isnan(x[i]) || std::isnan(y[i])) {
            ox.emplace_back();
            oy.emplace_back();
        } else {
            ox.emplace_back(x[i]);
            oy.emplace_back(y[i]);
        }
    }
    auto rx = factor::rank_pct(ox, true);
    auto ry = factor::rank_pct(oy, true);
    double mx = 0.0, my = 0.0, n = 0.0;
    for (size_t i = 0; i < rx.size(); ++i) {
        if (!std::isnan(rx[i]) && !std::isnan(ry[i])) {
            mx += rx[i]; my += ry[i]; n += 1.0;
        }
    }
    if (n < 3.0) return std::numeric_limits<double>::quiet_NaN();
    mx /= n; my /= n;
    double num = 0.0, denx = 0.0, deny = 0.0;
    for (size_t i = 0; i < rx.size(); ++i) {
        if (!std::isnan(rx[i]) && !std::isnan(ry[i])) {
            double dx = rx[i] - mx;
            double dy = ry[i] - my;
            num += dx * dy;
            denx += dx * dx;
            deny += dy * dy;
        }
    }
    double den = std::sqrt(denx * deny);
    if (den < 1e-12) return std::numeric_limits<double>::quiet_NaN();
    return num / den;
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    const std::string start_date = "2024-01-01";
    const std::string end_date = "2025-12-31";
    const int min_bars = 120;
    const int holding_days = 20;
    const int num_groups = 5;

    std::vector<std::string> ic_factors = {"momentum_20d", "volatility", "rsi_14"};

    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("多因子评价框架\n");
    fmt::print("{0}\n", std::string(70, '='));

    fmt::print("\n正在加载全市场数据...\n");
    auto all_data = bt::data::load_all_from_mysql(cfg);
    if (all_data.empty()) {
        fmt::print("无全市场数据\n");
        return 1;
    }

    // Filter by date range and minimum bars.
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

    // Build union of all trade dates.
    std::vector<std::string> all_dates;
    {
        std::set<std::string> date_set;
        for (const auto& kv : data) {
            for (const auto& b : kv.second) date_set.insert(b.date);
        }
        all_dates.assign(date_set.begin(), date_set.end());
    }
    auto reb_dates = month_end_dates(all_dates);
    fmt::print("  有效股票数: {} | 回测期数: {}\n", data.size(), reb_dates.size());

    // For each factor, compute IC series and quintile returns.
    for (const auto& fname : ic_factors) {
        fmt::print("\n{0}\n", std::string(60, '-'));
        fmt::print("因子: {}\n", fname);
        fmt::print("{0}\n", std::string(60, '-'));

        std::vector<double> ic_series;
        std::vector<std::vector<double>> group_returns(num_groups);

        for (const auto& rd : reb_dates) {
            std::vector<std::string> codes;
            std::vector<double> fvals;
            std::vector<double> fwd_rets;
            for (const auto& kv : data) {
                const auto& bars = kv.second;
                auto it = std::lower_bound(bars.begin(), bars.end(), rd,
                    [](const bt::Bar& b, const std::string& d) { return b.date < d; });
                if (it == bars.end() || it->date != rd) continue;
                size_t idx = static_cast<size_t>(it - bars.begin());
                if (idx + holding_days >= bars.size()) continue;

                std::vector<bt::Bar> slice(bars.begin(), it + 1);
                auto fac = factor::calc_all_factors(slice);
                auto fit = fac.find(fname);
                if (fit == fac.end() || std::isnan(fit->second)) continue;

                double fwd = bars[idx + holding_days].close / bars[idx].close - 1.0;
                codes.push_back(kv.first);
                fvals.push_back(fit->second);
                fwd_rets.push_back(fwd);
            }
            if (fvals.size() < num_groups * 2) continue;

            double ic = spearman(fvals, fwd_rets);
            if (!std::isnan(ic)) ic_series.push_back(ic);

            // Quintiles.
            std::vector<size_t> order(fvals.size());
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(),
                      [&fvals](size_t a, size_t b) { return fvals[a] < fvals[b]; });
            size_t per = order.size() / num_groups;
            size_t rem = order.size() % num_groups;
            size_t pos = 0;
            for (int g = 0; g < num_groups; ++g) {
                size_t cnt = per + (static_cast<size_t>(g) < rem ? 1 : 0);
                double sum = 0.0;
                size_t n = 0;
                for (size_t k = 0; k < cnt && pos < order.size(); ++k, ++pos) {
                    sum += fwd_rets[order[pos]];
                    ++n;
                }
                if (n > 0) group_returns[g].push_back(sum / static_cast<double>(n));
            }
        }

        if (!ic_series.empty()) {
            double mean_ic = std::accumulate(ic_series.begin(), ic_series.end(), 0.0) / ic_series.size();
            double sq_sum = 0.0;
            for (double v : ic_series) sq_sum += (v - mean_ic) * (v - mean_ic);
            double std_ic = std::sqrt(sq_sum / ic_series.size());
            int pos_ic = std::count_if(ic_series.begin(), ic_series.end(), [](double v) { return v > 0.0; });
            double icir = (std_ic > 0.0) ? mean_ic / std_ic : 0.0;
            fmt::print("  IC均值: {:.4f} | IC标准差: {:.4f} | ICIR: {:.4f} | IC>0比例: {:.1f}% | 期数: {}\n",
                       mean_ic, std_ic, icir, 100.0 * pos_ic / ic_series.size(), ic_series.size());
            std::string strength;
            if (std::abs(mean_ic) > 0.10) strength = "强";
            else if (std::abs(mean_ic) > 0.05) strength = "中等";
            else strength = "较弱";
            fmt::print("  因子强度: {}\n", strength);
        }

        fmt::print("\n  五分位分层回测 (Q1=最低, Q5=最高):\n");
        fmt::print("  {:<6} {:>12} {:>12}\n", "分组", "平均月收益", "累计收益");
        for (int g = 0; g < num_groups; ++g) {
            double avg = 0.0, cum = 1.0;
            for (double r : group_returns[g]) {
                avg += r;
                cum *= (1.0 + r);
            }
            if (!group_returns[g].empty()) avg /= group_returns[g].size();
            fmt::print("  Q{:<5} {:>+10.2f}% {:>+10.2f}%\n",
                       g + 1, avg * 100.0, (cum - 1.0) * 100.0);
        }
    }

    return 0;
}
