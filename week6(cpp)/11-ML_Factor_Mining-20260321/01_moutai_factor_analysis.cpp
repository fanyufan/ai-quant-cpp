// 1-贵州茅台因子分析
// 对应 week6/11-机器学习因子挖掘-20260321/1-贵州茅台因子分析.py

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "backtest.hpp"
#include "backtest_data_mysql.hpp"
#include "env.hpp"
#include "ml_features.hpp"
#include "ml_preprocessing.hpp"

using namespace quant;

namespace {

double spearman(const std::vector<double>& x, const std::vector<double>& y) {
    size_t n = x.size();
    if (n == 0 || n != y.size()) return std::numeric_limits<double>::quiet_NaN();

    std::vector<std::pair<double, size_t>> px, py;
    px.reserve(n); py.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(x[i]) && !std::isnan(y[i])) {
            px.emplace_back(x[i], i);
            py.emplace_back(y[i], i);
        }
    }
    if (px.size() < 3) return std::numeric_limits<double>::quiet_NaN();

    auto rank = [n](std::vector<std::pair<double, size_t>>& v) {
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<double> r(n, std::numeric_limits<double>::quiet_NaN());
        size_t i = 0;
        while (i < v.size()) {
            size_t j = i;
            while (j < v.size() && v[j].first == v[i].first) ++j;
            double avg = (static_cast<double>(i) + 1.0 + static_cast<double>(j)) / 2.0;
            for (size_t k = i; k < j; ++k) r[v[k].second] = avg;
            i = j;
        }
        return r;
    };

    auto rx = rank(px);
    auto ry = rank(py);

    std::vector<double> xs, ys;
    xs.reserve(px.size());
    ys.reserve(py.size());
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(rx[i]) && !std::isnan(ry[i])) {
            xs.push_back(rx[i]);
            ys.push_back(ry[i]);
        }
    }
    if (xs.size() < 3) return std::numeric_limits<double>::quiet_NaN();

    double mx = 0.0, my = 0.0;
    for (size_t i = 0; i < xs.size(); ++i) { mx += xs[i]; my += ys[i]; }
    mx /= xs.size(); my /= xs.size();

    double num = 0.0, denx = 0.0, deny = 0.0;
    for (size_t i = 0; i < xs.size(); ++i) {
        double dx = xs[i] - mx;
        double dy = ys[i] - my;
        num += dx * dy;
        denx += dx * dx;
        deny += dy * dy;
    }
    double den = std::sqrt(denx * deny);
    if (den < 1e-12) return std::numeric_limits<double>::quiet_NaN();
    return num / den;
}

std::vector<double> forward_return_1d(const std::vector<double>& close) {
    size_t n = close.size();
    std::vector<double> out(n, std::numeric_limits<double>::quiet_NaN());
    for (size_t i = 0; i + 1 < n; ++i) {
        if (close[i] != 0.0) out[i] = close[i + 1] / close[i] - 1.0;
    }
    return out;
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    const std::string stock_code = "600519.SH";
    const std::string start_date = "2023-01-01";
    const std::string end_date = "2025-12-31";

    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  贵州茅台因子分析\n");
    fmt::print("{0}\n", std::string(70, '='));

    // 1. Factor taxonomy
    fmt::print("\n[1] FACTOR_TAXONOMY\n");
    auto taxonomy = ml::feature_taxonomy();
    size_t total_features = 0;
    for (const auto& kv : taxonomy) {
        const auto& feats = kv.second;
        total_features += feats.size();
        std::string feat_str;
        for (size_t i = 0; i < feats.size() && i < 5; ++i) {
            if (i) feat_str += ", ";
            feat_str += feats[i];
        }
        if (feats.size() > 5) feat_str += fmt::format(" ... (共{}个)", feats.size());
        fmt::print("  {} ({}): {} 个 | {}\n", kv.first, kv.first, feats.size(), feat_str);
    }
    fmt::print("  技术因子合计: {} | 课件中另含 4 个基本面定义\n", total_features);

    // 2. Load stock data and compute technical features
    fmt::print("\n[2] load_stock_data + calc_features | {} {}~{}\n", stock_code, start_date, end_date);
    auto bars = bt::data::load_from_mysql(cfg, stock_code, start_date, end_date);
    if (bars.empty()) {
        fmt::print("  无法从 MySQL 加载 {} 数据\n", stock_code);
        return 1;
    }
    fmt::print("  交易日: {} | {} ~ {}\n", bars.size(), bars.front().date, bars.back().date);

    double min_close = bars.front().close, max_close = bars.front().close;
    for (const auto& b : bars) {
        min_close = std::min(min_close, b.close);
        max_close = std::max(max_close, b.close);
    }
    fmt::print("  close: {:.2f} ~ {:.2f}\n", min_close, max_close);

    auto features = ml::calc_features(bars);
    auto all_cols = ml::all_feature_names();
    std::vector<std::string> available_tech;
    for (const auto& c : all_cols) {
        if (features.find(c) != features.end()) available_tech.push_back(c);
    }
    fmt::print("  技术因子列数: {}\n", available_tech.size());

    std::vector<double> close;
    close.reserve(bars.size());
    for (const auto& b : bars) close.push_back(b.close);
    auto fwd_ret = forward_return_1d(close);

    // 3. Fundamental features
    fmt::print("\n[3] load_financial_data + calc_fundamental_features\n");
    std::vector<std::string> fundamental_cols;
    auto fin_all = bt::data::load_financial_data(cfg, {"eps", "roe", "gross_margin", "debt_ratio"}, "2022-01-01");
    if (fin_all.empty()) {
        fmt::print("  财务表为空, 跳过\n");
    } else {
        size_t total_records = 0;
        for (const auto& kv : fin_all) {
            for (const auto& fkv : kv.second) total_records += fkv.second.size();
        }
        fmt::print("  财务记录: {} | 股票数: {}\n", total_records, fin_all.size());

        auto it = fin_all.find(stock_code);
        std::map<std::string, ml::FinancialSeries> fin_data;
        if (it != fin_all.end()) {
            for (const auto& fkv : it->second) {
                ml::FinancialSeries s;
                for (const auto& rec : fkv.second) s.emplace_back(rec.date, rec.value);
                fin_data[fkv.first] = std::move(s);
            }
        }
        auto fund_features = ml::calc_fundamental_features(bars, fin_data);
        for (const auto& kv : fund_features) {
            features[kv.first] = kv.second;
            size_t non_nan = 0;
            for (double v : kv.second) if (!std::isnan(v)) ++non_nan;
            if (non_nan > 0) fundamental_cols.push_back(kv.first);
        }
        fmt::print("  合并基本面列: ");
        for (size_t i = 0; i < fundamental_cols.size(); ++i) {
            if (i) fmt::print(", ");
            fmt::print("{}", fundamental_cols[i]);
        }
        fmt::print("\n");
    }

    // 4. Industry one-hot demo
    fmt::print("\n[4] 行业哑变量示例 get_dummies\n");
    fmt::print("  {:<12s} {:<10s} ", "stock_code", "stock_name");
    std::vector<std::string> demo_industries = {"食品饮料", "非银金融", "银行"};
    for (const auto& ind : demo_industries) fmt::print("ind_{} ", ind);
    fmt::print("\n");

    std::vector<std::tuple<std::string, std::string, std::string>> demo_stocks = {
        {"600519.SH", "贵州茅台", "食品饮料"},
        {"000858.SZ", "五粮液", "食品饮料"},
        {"601318.SH", "中国平安", "非银金融"},
        {"600036.SH", "招商银行", "银行"},
        {"000001.SZ", "平安银行", "银行"},
    };
    for (const auto& [code, name, ind] : demo_stocks) {
        fmt::print("  {:<12s} {:<10s} ", code, name);
        for (const auto& dind : demo_industries) {
            fmt::print("{:>8d} ", ind == dind ? 1 : 0);
        }
        fmt::print("\n");
    }
    fmt::print("  哑变量列数: {}\n", demo_industries.size());

    // 5. Single-factor RankIC
    fmt::print("\n[5] 单因子 RankIC vs fwd_ret_1d\n");
    std::vector<std::string> all_factor_cols = available_tech;
    all_factor_cols.insert(all_factor_cols.end(), fundamental_cols.begin(), fundamental_cols.end());

    struct IcResult {
        std::string factor;
        std::string category;
        double rank_ic = 0.0;
        double abs_ic = 0.0;
    };
    std::vector<IcResult> ic_results;

    for (const auto& col : all_factor_cols) {
        auto fit = features.find(col);
        if (fit == features.end()) continue;
        const auto& fvals = fit->second;
        if (fvals.size() != fwd_ret.size()) continue;

        size_t valid = 0;
        for (size_t i = 0; i < fvals.size(); ++i) {
            if (!std::isnan(fvals[i]) && !std::isnan(fwd_ret[i])) ++valid;
        }
        if (valid < 30) continue;

        double ic = spearman(fvals, fwd_ret);
        if (std::isnan(ic)) continue;

        std::string cat_name = "基本面";
        for (const auto& kv : taxonomy) {
            const auto& feats = kv.second;
            if (std::find(feats.begin(), feats.end(), col) != feats.end()) {
                cat_name = kv.first;
                break;
            }
        }
        ic_results.push_back({col, cat_name, ic, std::abs(ic)});
    }

    std::sort(ic_results.begin(), ic_results.end(),
              [](const IcResult& a, const IcResult& b) { return a.abs_ic > b.abs_ic; });

    // 6. Summary
    fmt::print("\n[6] 完成检验因子数: {} | {}\n", ic_results.size(), stock_code);
    size_t top_n = std::min<size_t>(15, ic_results.size());
    fmt::print("\nTOP {} by |IC|\n", top_n);
    fmt::print("  {:>4s} {:<25s} {:<14s} {:>10s} {:>10s}\n",
               "排名", "factor", "category", "RankIC", "|IC|");
    fmt::print("  {0}\n", std::string(70, '-'));
    for (size_t i = 0; i < top_n; ++i) {
        fmt::print("  {:>4d} {:<25s} {:<14s} {:>+10.4f} {:>10.4f}\n",
                   i + 1, ic_results[i].factor, ic_results[i].category,
                   ic_results[i].rank_ic, ic_results[i].abs_ic);
    }

    size_t strong = 0, effective = 0, weak = 0, ineffective = 0;
    for (const auto& r : ic_results) {
        if (r.abs_ic >= 0.05) ++strong;
        else if (r.abs_ic >= 0.03) ++effective;
        else if (r.abs_ic >= 0.02) ++weak;
        else ++ineffective;
    }
    fmt::print("\n分档: >=0.05={} | [0.03,0.05)={} | [0.02,0.03)={} | <0.02={}\n",
               strong, effective, weak, ineffective);

    std::map<std::string, std::vector<double>> cat_ics;
    for (const auto& r : ic_results) cat_ics[r.category].push_back(r.abs_ic);

    fmt::print("\n按类别 |IC|\n");
    fmt::print("  {:<14s} {:>12s} {:>12s} {:>10s}\n", "category", "平均|IC|", "最大|IC|", "因子数");
    fmt::print("  {0}\n", std::string(55, '-'));
    std::vector<std::pair<std::string, double>> cat_avg;
    for (const auto& kv : cat_ics) {
        double mean = std::accumulate(kv.second.begin(), kv.second.end(), 0.0) / kv.second.size();
        cat_avg.emplace_back(kv.first, mean);
    }
    std::sort(cat_avg.begin(), cat_avg.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [cat, mean] : cat_avg) {
        (void)mean;
        double mx = *std::max_element(cat_ics[cat].begin(), cat_ics[cat].end());
        fmt::print("  {:<14s} {:>12.4f} {:>12.4f} {:>10d}\n",
                   cat, mean, mx, cat_ics[cat].size());
    }

    return 0;
}
