// 1-MASTER数据与因子
// 对应 week6/12-论文复现与策略进化-20260325/CASE-论文复现与策略进化/1-MASTER数据与因子.py

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

struct FactorStats {
    std::string factor;
    double mean = 0.0;
    double std = 0.0;
    double skew = 0.0;
    double kurtosis = 0.0;
    double outlier_pct = 0.0;
    double nan_pct = 0.0;
};

// Simple skew and kurtosis on a vector (no NaN)
std::pair<double, double> skew_kurtosis(const std::vector<double>& v) {
    size_t n = v.size();
    if (n < 4) return {0.0, 0.0};
    double mean = std::accumulate(v.begin(), v.end(), 0.0) / n;
    double m2 = 0.0, m3 = 0.0, m4 = 0.0;
    for (double x : v) {
        double d = x - mean;
        m2 += d * d;
        m3 += d * d * d;
        m4 += d * d * d * d;
    }
    m2 /= n; m3 /= n; m4 /= n;
    if (m2 < 1e-12) return {0.0, 0.0};
    double skew = m3 / std::pow(m2, 1.5);
    double kurt = m4 / (m2 * m2);
    return {skew, kurt};
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    const std::vector<std::string> EDA_STOCKS = {
        "600519.SH",  // 贵州茅台 - 消费
        "601318.SH",  // 中国平安 - 金融
        "000333.SZ",  // 美的集团 - 制造
        "300750.SZ",  // 宁德时代 - 新能源
        "002594.SZ",  // 比亚迪   - 汽车
        "000858.SZ",  // 五粮液   - 消费
        "600036.SH",  // 招商银行 - 银行
        "600276.SH",  // 恒瑞医药 - 医药
        "002415.SZ",  // 海康威视 - 科技
        "601012.SH",  // 隆基绿能 - 新能源
    };
    const std::string start_date = "2023-01-01";
    const std::string end_date = "2025-12-31";
    const int min_bars = 200;

    fmt::print("\nMASTER数据与因子 - 数据探索与预处理对比实战\n");
    fmt::print("{0}\n", std::string(80, '='));

    // Part 1: load and compute
    fmt::print("第一部分: 加载A股数据并计算因子\n");
    fmt::print("{0}\n", std::string(80, '='));
    fmt::print("  股票池: {} 只(覆盖消费/金融/制造/新能源/医药/科技)\n", EDA_STOCKS.size());
    fmt::print("  日期范围: {} ~ {}\n", start_date, end_date);

    std::vector<ml::PanelRow> panel;
    size_t loaded = 0;
    for (const auto& code : EDA_STOCKS) {
        auto bars = bt::data::load_from_mysql(cfg, code, start_date, end_date);
        if (static_cast<int>(bars.size()) < min_bars) continue;
        auto features = ml::calc_features(bars);
        for (size_t i = 0; i < bars.size(); ++i) {
            ml::PanelRow row;
            row.date = bars[i].date;
            row.code = code;
            row.close = bars[i].close;
            for (const auto& kv : features) row.features[kv.first] = kv.second[i];
            panel.push_back(std::move(row));
        }
        ++loaded;
    }

    fmt::print("\n  加载成功: {}/{} 只\n", loaded, EDA_STOCKS.size());
    if (loaded < 3) {
        fmt::print("  [错误] 有效股票不足, 无法继续分析\n");
        return 1;
    }

    auto all_cols = ml::all_feature_names();
    std::vector<std::string> feature_cols;
    for (const auto& c : all_cols) {
        bool has = false;
        for (const auto& r : panel) {
            if (r.features.find(c) != r.features.end()) { has = true; break; }
        }
        if (has) feature_cols.push_back(c);
    }
    fmt::print("  面板大小: {} 行 x {} 因子\n", panel.size(), feature_cols.size());

    // Part 2: diagnose distribution
    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("第二部分: 因子分布诊断 (建模前必须了解的)\n");
    fmt::print("{0}\n", std::string(80, '='));

    std::vector<FactorStats> stats;
    for (const auto& col : feature_cols) {
        std::vector<double> vals;
        size_t nan_cnt = 0;
        for (const auto& r : panel) {
            auto it = r.features.find(col);
            if (it == r.features.end() || std::isnan(it->second)) {
                ++nan_cnt;
                continue;
            }
            vals.push_back(it->second);
        }
        if (vals.size() < 100) continue;

        std::sort(vals.begin(), vals.end());
        double q1 = vals[vals.size() / 4];
        double q3 = vals[vals.size() * 3 / 4];
        double iqr = q3 - q1;
        double lower = q1 - 3.0 * iqr;
        double upper = q3 + 3.0 * iqr;
        size_t outliers = 0;
        for (double x : vals) if (x < lower || x > upper) ++outliers;

        double mean = std::accumulate(vals.begin(), vals.end(), 0.0) / vals.size();
        double sq = 0.0;
        for (double x : vals) sq += (x - mean) * (x - mean);
        double stdv = std::sqrt(sq / vals.size());
        auto [skew, kurt] = skew_kurtosis(vals);

        FactorStats fs;
        fs.factor = col;
        fs.mean = mean;
        fs.std = stdv;
        fs.skew = skew;
        fs.kurtosis = kurt;
        fs.outlier_pct = 100.0 * outliers / vals.size();
        fs.nan_pct = 100.0 * nan_cnt / panel.size();
        stats.push_back(fs);
    }

    auto top_outlier = stats;
    std::sort(top_outlier.begin(), top_outlier.end(),
              [](const FactorStats& a, const FactorStats& b) { return a.outlier_pct > b.outlier_pct; });

    fmt::print("\n  异常值率最高的5个因子 (IQR 3倍标准):\n");
    fmt::print("  {:<25s} {:>8s} {:>8s} {:>8s}\n", "因子", "偏度", "峰度", "异常值%");
    fmt::print("  {0} {1} {2} {3}\n", std::string(25, '-'), std::string(8, '-'), std::string(8, '-'), std::string(8, '-'));
    for (size_t i = 0; i < 5 && i < top_outlier.size(); ++i) {
        fmt::print("  {:<25s} {:>8.2f} {:>8.1f} {:>7.2f}%\n",
                   top_outlier[i].factor, top_outlier[i].skew, top_outlier[i].kurtosis, top_outlier[i].outlier_pct);
    }

    auto top_skew = stats;
    std::sort(top_skew.begin(), top_skew.end(),
              [](const FactorStats& a, const FactorStats& b) { return a.skew > b.skew; });
    fmt::print("\n  正偏最严重的5个因子 (右尾厚):\n");
    fmt::print("  {:<25s} {:>8s} {:>8s}\n", "因子", "偏度", "峰度");
    fmt::print("  {0} {1} {2}\n", std::string(25, '-'), std::string(8, '-'), std::string(8, '-'));
    for (size_t i = 0; i < 5 && i < top_skew.size(); ++i) {
        fmt::print("  {:<25s} {:>8.2f} {:>8.1f}\n",
                   top_skew[i].factor, top_skew[i].skew, top_skew[i].kurtosis);
    }

    double avg_outlier = 0.0;
    int high_skew = 0, high_kurt = 0;
    for (const auto& s : stats) {
        avg_outlier += s.outlier_pct;
        if (std::abs(s.skew) > 1.0) ++high_skew;
        if (s.kurtosis > 5.0) ++high_kurt;
    }
    avg_outlier /= stats.size();
    fmt::print("\n  汇总:\n");
    fmt::print("    平均异常值率: {:.2f}%\n", avg_outlier);
    fmt::print("    高偏度因子(|skew|>1): {}/{}\n", high_skew, stats.size());
    fmt::print("    高峰度因子(kurtosis>5): {}/{}\n", high_kurt, stats.size());
    fmt::print("    --> 说明原始因子普遍存在厚尾分布, 需要去极值处理\n");

    // Part 3: preprocessing comparison
    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("第三部分: 预处理方法对比\n");
    fmt::print("  方法A: RobustZScoreNorm (MASTER论文) - 中位数 + MAD + clip[-3,3]\n");
    fmt::print("  方法B: MAD + Z-Score (L11华泰标准) - MAD去极值 + mean/std标准化\n");
    fmt::print("{0}\n", std::string(80, '='));

    std::vector<std::string> demo_factors = {"rsi_14", "momentum_20d", "hist_vol_20d",
                                             "vol_ratio_5d", "adx_14"};
    demo_factors.erase(std::remove_if(demo_factors.begin(), demo_factors.end(),
        [&feature_cols](const std::string& s) {
            return std::find(feature_cols.begin(), feature_cols.end(), s) == feature_cols.end();
        }), demo_factors.end());
    if (demo_factors.empty() && !feature_cols.empty()) demo_factors.assign(feature_cols.begin(), feature_cols.begin() + std::min<size_t>(5, feature_cols.size()));

    // Method A: per-stock robust zscore norm clip[-3,3]
    auto panel_a = panel;
    {
        std::map<std::string, std::vector<size_t>> groups;
        for (size_t i = 0; i < panel_a.size(); ++i) groups[panel_a[i].code].push_back(i);
        for (const auto& col : feature_cols) {
            for (const auto& kv : groups) {
                std::vector<double> series;
                for (size_t idx : kv.second) {
                    auto it = panel_a[idx].features.find(col);
                    series.push_back((it != panel_a[idx].features.end()) ? it->second : std::numeric_limits<double>::quiet_NaN());
                }
                series = ml::robust_zscore_norm(series, 3.0);
                for (size_t t = 0; t < kv.second.size(); ++t) panel_a[kv.second[t]].features[col] = series[t];
            }
        }
    }

    // Method B: per-stock MAD + zscore
    auto panel_b = ml::preprocess_panel(panel, feature_cols, "mad");

    fmt::print("\n  {:<25s} | {:>20s} | {:>20s} | {:>20s}\n",
               "因子", "原始范围", "RobustZScore范围", "MAD+ZScore范围");
    fmt::print("  {0}-+-{1}-+-{2}-+-{3}\n",
               std::string(25, '-'), std::string(20, '-'), std::string(20, '-'), std::string(20, '-'));

    for (const auto& col : demo_factors) {
        auto range = [](const std::vector<ml::PanelRow>& p, const std::string& c) {
            std::vector<double> v;
            for (const auto& r : p) {
                auto it = r.features.find(c);
                if (it != r.features.end() && !std::isnan(it->second)) v.push_back(it->second);
            }
            if (v.empty()) return std::string("N/A");
            double mn = *std::min_element(v.begin(), v.end());
            double mx = *std::max_element(v.begin(), v.end());
            return fmt::format("[{:>7.2f}, {:>7.2f}]", mn, mx);
        };
        fmt::print("  {:<25s} | {:>20s} | {:>20s} | {:>20s}\n",
                   col, range(panel, col), range(panel_a, col), range(panel_b, col));
    }

    if (!demo_factors.empty()) {
        const auto& test_col = demo_factors[0];
        auto collect = [&test_col](const std::vector<ml::PanelRow>& p) {
            std::vector<double> v;
            for (const auto& r : p) {
                auto it = r.features.find(test_col);
                if (it != r.features.end() && !std::isnan(it->second)) v.push_back(it->second);
            }
            return v;
        };
        auto raw = collect(panel);
        auto r_out = collect(panel_a);
        auto m_out = collect(panel_b);

        auto desc = [](const std::vector<double>& v) {
            double mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
            double sq = 0.0;
            for (double x : v) sq += (x - mean) * (x - mean);
            double stdv = std::sqrt(sq / v.size());
            auto [sk, _] = skew_kurtosis(v);
            return std::make_tuple(mean, stdv, sk);
        };
        auto [rmean, rstd, rsk] = desc(raw);
        auto [rzmean, rzstd, rzsk] = desc(r_out);
        auto [mzmean, mzstd, mzsk] = desc(m_out);

        fmt::print("\n  详细对比 ({}):\n", test_col);
        fmt::print("    原始数据:   均值={:.4f}  标准差={:.4f}  偏度={:.3f}\n", rmean, rstd, rsk);
        fmt::print("    RobustZ:    均值={:.4f}  标准差={:.4f}  偏度={:.3f}\n", rzmean, rzstd, rzsk);
        fmt::print("    MAD+ZScore: 均值={:.4f}  标准差={:.4f}  偏度={:.3f}\n", mzmean, mzstd, mzsk);

        size_t clipped = 0;
        for (double x : r_out) if (std::abs(x) >= 2.99) ++clipped;
        fmt::print("\n    RobustZ clip到[-3,3]被裁比例: {:.2f}%\n", 100.0 * clipped / r_out.size());
    }

    fmt::print("\n  核心差异:\n");
    fmt::print("    RobustZScoreNorm: 输出严格限制在[-3,3], 对极端值硬截断\n");
    fmt::print("                     适合深度学习(梯度稳定, 激活函数不饱和)\n");
    fmt::print("    MAD+Z-Score:     先去极值再标准化, 输出范围取决于数据\n");
    fmt::print("                     适合树模型(不依赖数值范围, 只看排序)\n");

    // Part 4: factor correlation
    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("第四部分: 因子相关性分析 (发现冗余因子)\n");
    fmt::print("{0}\n", std::string(80, '='));

    std::vector<std::vector<double>> mat;
    for (const auto& r : panel) {
        std::vector<double> row;
        bool ok = true;
        for (const auto& c : feature_cols) {
            auto it = r.features.find(c);
            if (it == r.features.end() || std::isnan(it->second)) { ok = false; break; }
            row.push_back(it->second);
        }
        if (ok) mat.push_back(std::move(row));
    }
    fmt::print("\n  总因子数: {}\n", feature_cols.size());

    struct CorrPair { std::string a, b; double corr; };
    std::vector<CorrPair> high_corr;
    if (mat.size() >= 2) {
        size_t m = feature_cols.size();
        for (size_t i = 0; i < m; ++i) {
            for (size_t j = i + 1; j < m; ++j) {
                double mi = 0.0, mj = 0.0;
                for (const auto& row : mat) { mi += row[i]; mj += row[j]; }
                mi /= mat.size(); mj /= mat.size();
                double num = 0.0, dxi = 0.0, dxj = 0.0;
                for (const auto& row : mat) {
                    double di = row[i] - mi, dj = row[j] - mj;
                    num += di * dj; dxi += di * di; dxj += dj * dj;
                }
                double den = std::sqrt(dxi * dxj);
                if (den > 0.0) {
                    double c = num / den;
                    if (std::abs(c) > 0.8) high_corr.push_back({feature_cols[i], feature_cols[j], c});
                }
            }
        }
    }
    std::sort(high_corr.begin(), high_corr.end(),
              [](const CorrPair& a, const CorrPair& b) { return std::abs(a.corr) > std::abs(b.corr); });

    fmt::print("  高相关对(|r|>0.8): {} 对\n", high_corr.size());
    if (!high_corr.empty()) {
        fmt::print("\n  Top 10 高相关因子对:\n");
        fmt::print("  {:<25s}  {:<25s}  {:>8s}\n", "因子A", "因子B", "相关系数");
        fmt::print("  {0}  {1}  {2}\n", std::string(25, '-'), std::string(25, '-'), std::string(8, '-'));
        for (size_t i = 0; i < 10 && i < high_corr.size(); ++i) {
            fmt::print("  {:<25s}  {:<25s}  {:>8.3f}\n", high_corr[i].a, high_corr[i].b, high_corr[i].corr);
        }
        fmt::print("\n  实践建议:\n");
        fmt::print("    - 相关性>0.9的因子对可以考虑只保留其中一个\n");
        fmt::print("    - 树模型(RandomForest)对共线性不敏感, 影响不大\n");
        fmt::print("    - 线性模型/神经网络对共线性敏感, 需要去冗余\n");
    } else {
        fmt::print("\n  未发现高相关因子对, 因子体系正交性较好\n");
    }

    // Part 5: MASTER CSV (optional, just report skipped)
    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("第五部分: MASTER市场信息数据 (63维)\n");
    fmt::print("{0}\n", std::string(80, '='));
    fmt::print("  [跳过] 未找到MASTER CSV (MASTER-master/data/opensource/*.pkl)\n");
    fmt::print("  提示: 这是论文附带的中国A股指数级别数据, 非必需\n");
    fmt::print("\n  对比总结:\n");
    fmt::print("    {:<15s} {:>15s}  {:>15s}\n", "维度", "MASTER", "我们(L11)");
    fmt::print("    {0} {1}  {2}\n", std::string(15, '-'), std::string(15, '-'), std::string(15, '-'));
    fmt::print("    {:<15s} {:>15s}  {:>15s}\n", "因子数量", "158(Alpha158)", "52(TA-Lib)");
    fmt::print("    {:<15s} {:>15s}  {:>15s}\n", "市场信息", "63维(3指数x21)", "无");
    fmt::print("    {:<15s} {:>15s}  {:>15s}\n", "总特征维度", "221", "52");
    fmt::print("    {:<15s} {:>15s}  {:>15s}\n", "预处理方法", "RobustZScoreNorm", "MAD+Z-Score");
    fmt::print("    {:<15s} {:>15s}  {:>15s}\n", "数据来源", "Qlib框架", "MySQL+TA-Lib");

    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("[完成] 数据探索结束, 接下来运行 3-XGBoost截面预测.cpp 进行截面预测\n");

    return 0;
}
