// 2-工业级特征工程
// 对应 week6/11-机器学习因子挖掘-20260321/2-工业级特征工程.py

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "backtest.hpp"
#include "backtest_data_mysql.hpp"
#include "env.hpp"
#include "ml_features.hpp"
#include "ml_preprocessing.hpp"

using namespace quant;

namespace {

struct StockFeatures {
    std::string code;
    std::vector<bt::Bar> bars;
    std::map<std::string, std::vector<double>> features;
};

std::vector<double> rolling_mean(const std::vector<double>& vals, size_t period) {
    size_t n = vals.size();
    std::vector<double> out(n, std::numeric_limits<double>::quiet_NaN());
    if (n == 0 || period == 0) return out;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += vals[i];
        if (i >= period) sum -= vals[i - period];
        if (i >= period - 1) out[i] = sum / static_cast<double>(period);
    }
    return out;
}

void print_stats(const std::string& title,
                 const std::vector<ml::PanelRow>& panel,
                 const std::vector<std::string>& demo_factors) {
    fmt::print("\n{}\n", title);
    for (const auto& col : demo_factors) {
        std::vector<double> v;
        for (const auto& r : panel) {
            auto it = r.features.find(col);
            if (it != r.features.end() && !std::isnan(it->second)) v.push_back(it->second);
        }
        if (v.empty()) continue;
        double mn = *std::min_element(v.begin(), v.end());
        double mx = *std::max_element(v.begin(), v.end());
        double mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
        double sq = 0.0;
        for (double x : v) sq += (x - mean) * (x - mean);
        double stdv = std::sqrt(sq / v.size());
        fmt::print("  {:<20s}: min={:10.4f}  max={:10.4f}  mean={:10.4f}  std={:10.4f}\n",
                   col, mn, mx, mean, stdv);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    const std::vector<std::string> STOCK_POOL = {
        "600519.SH",   // 贵州茅台
        "688981.SH",   // 中芯国际
        "000001.SZ",   // 平安银行
        "159941.SZ",   // 纳指ETF
        "300750.SZ",   // 宁德时代
    };
    const std::map<std::string, std::string> INDUSTRY_MAP = {
        {"600519.SH", "食品饮料"},
        {"688981.SH", "半导体"},
        {"000001.SZ", "银行"},
        {"159941.SZ", "指数基金"},
        {"300750.SZ", "新能源"},
    };
    const std::string start_date = "2023-01-01";
    const std::string end_date = "2025-12-31";
    const int min_bars = 120;

    auto print_section = [](const std::string& title) {
        fmt::print("\n{0}\n", std::string(60, '='));
        fmt::print("  {}\n", title);
        fmt::print("{0}\n", std::string(60, '='));
    };

    // Step 1: load data
    print_section("第1步: 加载目标股票日K线");
    std::map<std::string, StockFeatures> stock_data;
    for (const auto& code : STOCK_POOL) {
        auto bars = bt::data::load_from_mysql(cfg, code, start_date, end_date);
        if (static_cast<int>(bars.size()) < min_bars) continue;
        StockFeatures sf;
        sf.code = code;
        sf.bars = std::move(bars);
        stock_data[code] = std::move(sf);
    }
    fmt::print("成功加载 {} 只股票\n", stock_data.size());
    for (const auto& kv : stock_data) {
        const auto& code = kv.first;
        const auto& bars = kv.second.bars;
        auto it = INDUSTRY_MAP.find(code);
        std::string name = (it != INDUSTRY_MAP.end()) ? it->second : "未知";
        fmt::print("  {} ({}): {} 个交易日, {} ~ {}\n",
                   code, name, bars.size(), bars.front().date, bars.back().date);
    }
    if (stock_data.empty()) {
        fmt::print("[警告] 未加载到任何目标股票, 请检查数据库数据\n");
        return 1;
    }

    // Step 2: technical features
    print_section("第2步: 计算50+技术特征 (calc_features)");
    for (auto& kv : stock_data) {
        kv.second.features = ml::calc_features(kv.second.bars);
    }
    auto taxonomy = ml::feature_taxonomy();
    size_t total_count = 0;
    fmt::print("因子分类体系 ({} 大类):\n", taxonomy.size());
    for (const auto& kv : taxonomy) {
        size_t n = kv.second.size();
        total_count += n;
        std::string fs;
        for (size_t i = 0; i < n && i < 4; ++i) {
            if (i) fs += ", ";
            fs += kv.second[i];
        }
        if (n > 4) fs += fmt::format(" ... 共{}个", n);
        fmt::print("  {} ({}): {} 个因子\n", kv.first, kv.first, n);
        fmt::print("    -> {}\n", fs);
    }
    auto sample_code = stock_data.begin()->first;
    auto all_cols = ml::all_feature_names();
    std::vector<std::string> available_features;
    for (const auto& c : all_cols) {
        if (stock_data[sample_code].features.find(c) != stock_data[sample_code].features.end())
            available_features.push_back(c);
    }
    fmt::print("\n因子总数: {} 个, 实际可用: {} 个\n", total_count, available_features.size());

    // Step 3: fundamental features
    print_section("第3步: 加载财务数据, 补充基本面因子");
    auto fin_all = bt::data::load_financial_data(cfg, {"eps", "roe", "gross_margin", "debt_ratio"}, "2022-01-01");
    if (fin_all.empty()) {
        fmt::print("[警告] 财务数据为空, 跳过基本面因子计算\n");
    } else {
        size_t total_records = 0;
        for (const auto& ckv : fin_all) {
            for (const auto& fkv : ckv.second) total_records += fkv.second.size();
        }
        fmt::print("加载财务数据: {} 条记录, 覆盖 {} 只股票\n", total_records, fin_all.size());
        for (auto& kv : stock_data) {
            const auto& code = kv.first;
            auto it = fin_all.find(code);
            std::map<std::string, ml::FinancialSeries> fin_data;
            if (it != fin_all.end()) {
                for (const auto& fkv : it->second) {
                    ml::FinancialSeries s;
                    for (const auto& rec : fkv.second) s.emplace_back(rec.date, rec.value);
                    fin_data[fkv.first] = std::move(s);
                }
            }
            auto fund = ml::calc_fundamental_features(kv.second.bars, fin_data);
            for (auto& fkv : fund) kv.second.features[fkv.first] = std::move(fkv.second);
        }
    }

    // Step 4: build panel with industry dummies
    print_section("第4步: 构造行业因子 (one-hot编码)");
    fmt::print("行业映射:\n");
    for (const auto& kv : INDUSTRY_MAP) fmt::print("  {} -> {}\n", kv.first, kv.second);

    std::set<std::string> ind_set;
    for (const auto& kv : INDUSTRY_MAP) ind_set.insert(kv.second);
    std::vector<std::string> industries(ind_set.begin(), ind_set.end());
    fmt::print("\n行业列表:");
    for (const auto& ind : industries) fmt::print(" {}", ind);
    fmt::print("\n");

    std::vector<ml::PanelRow> panel;
    for (const auto& kv : stock_data) {
        const auto& code = kv.first;
        const auto& bars = kv.second.bars;
        const auto& feats = kv.second.features;
        auto it_ind = INDUSTRY_MAP.find(code);
        std::string industry = (it_ind != INDUSTRY_MAP.end()) ? it_ind->second : "其他";

        for (size_t i = 0; i < bars.size(); ++i) {
            ml::PanelRow row;
            row.date = bars[i].date;
            row.code = code;
            row.close = bars[i].close;
            for (const auto& fkv : feats) {
                row.features[fkv.first] = fkv.second[i];
            }
            for (const auto& ind : industries) {
                row.industries[fmt::format("ind_{}", ind)] = (ind == industry) ? 1.0 : 0.0;
            }
            panel.push_back(std::move(row));
        }
    }

    std::vector<std::string> ind_cols;
    for (const auto& ind : industries) ind_cols.push_back(fmt::format("ind_{}", ind));

    fmt::print("\n合并后数据: {} 行 x {} 列\n", panel.size(),
               available_features.size() + ind_cols.size() + 3);
    fmt::print("行业哑变量列:");
    for (const auto& c : ind_cols) fmt::print(" {}", c);
    fmt::print("\n\n行业分布:\n");
    for (const auto& ind : industries) {
        const std::string col = fmt::format("ind_{}", ind);
        size_t cnt = 0;
        for (const auto& r : panel) {
            auto it = r.industries.find(col);
            if (it != r.industries.end() && it->second == 1.0) ++cnt;
        }
        fmt::print("  {}: {} 条记录\n", ind, cnt);
    }

    // Step 5: preprocess
    print_section("第5步: 华泰标准预处理 (MAD去极值 + Z-score标准化)");
    std::vector<std::string> demo_factors = {"momentum_20d", "rsi_14", "macd_hist"};
    demo_factors.erase(std::remove_if(demo_factors.begin(), demo_factors.end(),
        [&available_features](const std::string& s) {
            return std::find(available_features.begin(), available_features.end(), s) == available_features.end();
        }), demo_factors.end());

    print_stats("--- 去极值前的分布统计 ---", panel, demo_factors);
    auto preprocessed = ml::preprocess_panel(panel, available_features, "mad");
    print_stats("\n--- 去极值+标准化后的分布统计 ---", preprocessed, demo_factors);

    fmt::print("\n预处理效果:\n");
    fmt::print("  - 极端值被MAD方法截断 (中位数 +/- 5*1.4826*MAD)\n");
    fmt::print("  - 标准化后均值接近0, 标准差接近1\n");
    fmt::print("  - 不同因子量纲统一, 可直接输入模型\n");

    // Step 6: neutralize
    print_section("第6步: 行业市值中性化");
    fmt::print("原理: factor = beta_industry * industry + beta_mktcap * ln(mktcap) + residual\n");
    fmt::print("残差residual即为中性化后的因子值, 消除了行业和市值的影响\n\n");

    // Compute market cap proxy = log(close * volume MA20) per stock
    for (auto& r : preprocessed) {
        const auto& bars = stock_data[r.code].bars;
        std::vector<double> volumes;
        for (const auto& b : bars) volumes.push_back(b.volume);
        auto vol_ma20 = rolling_mean(volumes, 20);
        auto it = std::find_if(bars.begin(), bars.end(),
                               [&r](const bt::Bar& b) { return b.date == r.date; });
        if (it != bars.end()) {
            size_t idx = static_cast<size_t>(it - bars.begin());
            double proxy = r.close * vol_ma20[idx];
            if (proxy > 0.0) r.mktcap_log = std::log(proxy);
        }
    }

    std::string target_factor = "momentum_20d";
    std::vector<double> factor_before;
    factor_before.reserve(preprocessed.size());
    for (const auto& r : preprocessed) {
        auto it = r.features.find(target_factor);
        factor_before.push_back((it != r.features.end()) ? it->second : std::numeric_limits<double>::quiet_NaN());
    }

    auto neutralized = ml::neutralize_factor(preprocessed, target_factor, ind_cols, true);

    fmt::print("因子: {}\n", target_factor);
    {
        auto stats = [](const std::vector<double>& v) {
            std::vector<double> x;
            for (double val : v) if (!std::isnan(val)) x.push_back(val);
            double mn = *std::min_element(x.begin(), x.end());
            double mx = *std::max_element(x.begin(), x.end());
            double mean = std::accumulate(x.begin(), x.end(), 0.0) / x.size();
            double sq = 0.0;
            for (double val : x) sq += (val - mean) * (val - mean);
            double stdv = std::sqrt(sq / x.size());
            return std::make_tuple(mean, stdv, mn, mx);
        };
        auto [bmean, bstd, bmin, bmax] = stats(factor_before);
        fmt::print("\n--- 中性化前 ---\n");
        fmt::print("  mean={:.6f}  std={:.6f}  min={:.6f}  max={:.6f}\n", bmean, bstd, bmin, bmax);

        std::vector<double> factor_after;
        for (const auto& r : neutralized) {
            auto it = r.features.find(target_factor);
            factor_after.push_back((it != r.features.end()) ? it->second : std::numeric_limits<double>::quiet_NaN());
        }
        auto [amean, astd, amin, amax] = stats(factor_after);
        fmt::print("\n--- 中性化后 ---\n");
        fmt::print("  mean={:.6f}  std={:.6f}  min={:.6f}  max={:.6f}\n", amean, astd, amin, amax);
    }

    fmt::print("\n各行业因子均值对比:\n");
    fmt::print("  {:<10s} {:>12s} {:>12s}\n", "行业", "中性化前", "中性化后");
    fmt::print("  {0} {1} {2}\n", std::string(10, '-'), std::string(12, '-'), std::string(12, '-'));
    for (const auto& col : ind_cols) {
        std::string ind_name = col.substr(4);
        std::vector<double> before_means, after_means;
        for (size_t i = 0; i < neutralized.size(); ++i) {
            auto iit = neutralized[i].industries.find(col);
            if (iit != neutralized[i].industries.end() && iit->second == 1.0) {
                if (!std::isnan(factor_before[i])) before_means.push_back(factor_before[i]);
            }
        }
        if (before_means.empty()) continue;
        double mb = std::accumulate(before_means.begin(), before_means.end(), 0.0) / before_means.size();
        std::vector<double> after_vals;
        for (size_t i = 0; i < neutralized.size(); ++i) {
            auto iit = neutralized[i].industries.find(col);
            if (iit != neutralized[i].industries.end() && iit->second == 1.0) {
                auto fit = neutralized[i].features.find(target_factor);
                if (fit != neutralized[i].features.end() && !std::isnan(fit->second))
                    after_vals.push_back(fit->second);
            }
        }
        double ma = after_vals.empty() ? 0.0 :
            std::accumulate(after_vals.begin(), after_vals.end(), 0.0) / after_vals.size();
        fmt::print("  {:<10s} {:>12.6f} {:>12.6f}\n", ind_name, mb, ma);
    }
    fmt::print("\n中性化效果: 消除行业间的因子均值差异, 使因子反映个股相对行业的超额信息\n");

    // Step 7: correlation analysis
    print_section("第7步: 特征相关性分析");
    // Build matrix rows aligned to feature cols; skip rows with any NaN among avail_cols
    std::vector<std::vector<double>> mat;
    for (const auto& r : neutralized) {
        std::vector<double> row;
        bool ok = true;
        for (const auto& c : available_features) {
            auto it = r.features.find(c);
            if (it == r.features.end() || std::isnan(it->second)) { ok = false; break; }
            row.push_back(it->second);
        }
        if (ok) mat.push_back(std::move(row));
    }
    fmt::print("计算 {} 个特征的相关系数矩阵: {} x {} (有效样本 {})\n",
               available_features.size(), available_features.size(), available_features.size(), mat.size());

    const double threshold = 0.8;
    struct CorrPair {
        std::string a, b;
        double corr;
    };
    std::vector<CorrPair> high_corr;
    if (mat.size() >= 2) {
        size_t m = available_features.size();
        for (size_t i = 0; i < m; ++i) {
            for (size_t j = i + 1; j < m; ++j) {
                double mi = 0.0, mj = 0.0;
                for (const auto& row : mat) { mi += row[i]; mj += row[j]; }
                mi /= mat.size(); mj /= mat.size();
                double num = 0.0, dxi = 0.0, dxj = 0.0;
                for (const auto& row : mat) {
                    double di = row[i] - mi, dj = row[j] - mj;
                    num += di * dj;
                    dxi += di * di;
                    dxj += dj * dj;
                }
                double den = std::sqrt(dxi * dxj);
                if (den > 0.0) {
                    double c = num / den;
                    if (std::abs(c) > threshold) high_corr.push_back({available_features[i], available_features[j], c});
                }
            }
        }
    }
    std::sort(high_corr.begin(), high_corr.end(),
              [](const CorrPair& a, const CorrPair& b) { return std::abs(a.corr) > std::abs(b.corr); });

    fmt::print("\n高相关特征对 (|corr| > {}): 共 {} 对\n", threshold, high_corr.size());
    fmt::print("  {:<25s} {:<25s} {:>10s}\n", "特征A", "特征B", "相关系数");
    fmt::print("  {0} {1} {2}\n", std::string(25, '-'), std::string(25, '-'), std::string(10, '-'));
    size_t show_limit = 20;
    for (size_t i = 0; i < high_corr.size() && i < show_limit; ++i) {
        fmt::print("  {:<25s} {:<25s} {:>10.4f}\n",
                   high_corr[i].a, high_corr[i].b, high_corr[i].corr);
    }
    if (high_corr.size() > show_limit) {
        fmt::print("  ... 共 {} 对, 仅展示前 {} 对\n", high_corr.size(), show_limit);
    }

    std::set<std::string> redundant;
    for (const auto& p : high_corr) redundant.insert(p.b);
    fmt::print("\n冗余特征建议 (可考虑剔除): {} 个\n", redundant.size());
    for (const auto& f : redundant) fmt::print("  - {}\n", f);
    fmt::print("\n保留后特征数: {} 个 (原 {} 个)\n",
               available_features.size() - redundant.size(), available_features.size());

    // Summary
    fmt::print("\n特征工程流水线:\n");
    fmt::print("  原始OHLCV -> 50+因子 -> 去极值 -> 中性化 -> 标准化 -> 建模\n");
    fmt::print("\n各环节要点:\n");
    fmt::print("  1. 原始OHLCV: 从数据库批量加载日K线数据\n");
    fmt::print("  2. 50+因子:    calc_features() 计算价量/动量/波动率/技术/均线/交互 6大类因子\n");
    fmt::print("  3. 基本面因子: calc_fundamental_features() 补充PE/ROE/毛利率等\n");
    fmt::print("  4. 行业因子:   构造行业哑变量 (one-hot), 用于后续中性化\n");
    fmt::print("  5. MAD去极值:  中位数 +/- 5*1.4826*MAD 截断, 消除极端离群值\n");
    fmt::print("  6. 中性化:     回归法消除行业和市值对因子的影响\n");
    fmt::print("  7. Z-score:    标准化到均值0/标准差1, 统一量纲\n");
    fmt::print("  8. 相关性分析: 识别冗余特征, 降低多重共线性\n");

    return 0;
}
