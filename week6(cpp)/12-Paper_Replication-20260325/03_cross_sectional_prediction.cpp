// 3-XGBoost截面预测
// 对应 week6/12-论文复现与策略进化-20260325/CASE-论文复现与策略进化/3-XGBoost截面预测.py
// C++ 中使用自研 RandomForestClassifier(二分类概率) 作为截面预测模型。

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fmt/format.h>
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
#include "ml_tree.hpp"

using namespace quant;

namespace {

struct FactorIC {
    std::string factor;
    double ic = 0.0;
    double icir = 0.0;
    double rank_ic = 0.0;
    double rank_icir = 0.0;
    double ic_positive = 0.0;
    size_t n_days = 0;
};

double pearson(const std::vector<double>& a, const std::vector<double>& b) {
    size_t n = a.size();
    if (n == 0 || n != b.size()) return std::numeric_limits<double>::quiet_NaN();
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double num = 0.0, da = 0.0, db = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double xa = a[i] - ma, xb = b[i] - mb;
        num += xa * xb; da += xa * xa; db += xb * xb;
    }
    double den = std::sqrt(da * db);
    return den > 0.0 ? num / den : std::numeric_limits<double>::quiet_NaN();
}

double spearman(const std::vector<double>& a, const std::vector<double>& b) {
    size_t n = a.size();
    if (n == 0 || n != b.size()) return std::numeric_limits<double>::quiet_NaN();
    auto rank = [](const std::vector<double>& v) {
        std::vector<std::pair<double, size_t>> p;
        p.reserve(v.size());
        for (size_t i = 0; i < v.size(); ++i) p.emplace_back(v[i], i);
        std::sort(p.begin(), p.end());
        std::vector<double> r(v.size());
        size_t i = 0;
        while (i < p.size()) {
            size_t j = i;
            while (j < p.size() && p[j].first == p[i].first) ++j;
            double avg = (static_cast<double>(i) + 1.0 + static_cast<double>(j)) / 2.0;
            for (size_t k = i; k < j; ++k) r[p[k].second] = avg;
            i = j;
        }
        return r;
    };
    return pearson(rank(a), rank(b));
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    const std::vector<std::string> STOCK_POOL = {
        "600519.SH", "000858.SZ", "601318.SH", "600036.SH", "000333.SZ",
        "600900.SH", "601166.SH", "000001.SZ", "600276.SH", "601888.SH",
        "002594.SZ", "300750.SZ", "601398.SH", "601939.SH", "600030.SH",
        "000651.SZ", "002415.SZ", "600309.SH", "600887.SH", "601012.SH",
        "000568.SZ", "002304.SZ", "600050.SH", "601668.SH", "600000.SH",
        "000002.SZ", "601857.SH", "600585.SH", "002352.SZ", "600104.SH",
        "601601.SH", "600690.SH", "601288.SH", "600028.SH", "601138.SH",
        "002714.SZ", "300059.SZ", "002475.SZ", "600031.SH", "300760.SZ",
        "601899.SH", "600809.SH", "000725.SZ", "002230.SZ", "601919.SH",
        "300015.SZ", "002142.SZ", "600438.SH", "601225.SH", "002027.SZ",
    };
    const std::string start_date = "2023-01-01";
    const std::string end_date = "2025-12-31";
    const int min_bars = 200;
    const size_t predict_horizon = 5;
    const size_t train_window = 60;
    const size_t roll_step = 20;

    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("  截面预测与IC评估 -- RandomForest实践MASTER论文评估方法论\n");
    fmt::print("  论文: MASTER (AAAI 2024), 目标市场: 中国A股(CSI300/CSI800)\n");
    fmt::print("{0}\n", std::string(80, '='));

    // Part 1: load and compute factors
    fmt::print("第一部分: 加载A股数据并计算因子\n");
    fmt::print("{0}\n", std::string(80, '='));
    fmt::print("  股票池: {} 只代表性A股大盘股\n", STOCK_POOL.size());
    fmt::print("  日期范围: {} ~ {}\n", start_date, end_date);
    fmt::print("  因子引擎: L11 feature_engine (TA-Lib, 50+维)\n");

    std::vector<ml::PanelRow> panel;
    size_t loaded = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const auto& code : STOCK_POOL) {
        auto bars = bt::data::load_from_mysql(cfg, code, start_date, end_date);
        if (static_cast<int>(bars.size()) < min_bars) continue;
        auto features = ml::calc_features(bars);
        for (size_t i = 0; i + predict_horizon < bars.size(); ++i) {
            ml::PanelRow row;
            row.date = bars[i].date;
            row.code = code;
            row.close = bars[i].close;
            if (bars[i].close != 0.0) {
                row.features["future_ret"] = bars[i + predict_horizon].close / bars[i].close - 1.0;
            } else {
                row.features["future_ret"] = std::numeric_limits<double>::quiet_NaN();
            }
            for (const auto& kv : features) row.features[kv.first] = kv.second[i];
            panel.push_back(std::move(row));
        }
        ++loaded;
    }
    auto t1 = std::chrono::steady_clock::now();
    fmt::print("\n  成功加载: {}/{} 只, 耗时: {:.1f}s\n", loaded, STOCK_POOL.size(),
               std::chrono::duration<double>(t1 - t0).count());
    if (loaded < 10) {
        fmt::print("  [错误] 有效股票不足10只, 无法进行截面分析\n");
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

    std::set<std::string> date_set;
    for (const auto& r : panel) date_set.insert(r.date);
    std::vector<std::string> dates(date_set.begin(), date_set.end());

    double mean_ret = 0.0, std_ret = 0.0;
    size_t ret_cnt = 0;
    for (const auto& r : panel) {
        auto it = r.features.find("future_ret");
        if (it != r.features.end() && !std::isnan(it->second)) {
            mean_ret += it->second; ++ret_cnt;
        }
    }
    mean_ret /= ret_cnt;
    double sq = 0.0;
    for (const auto& r : panel) {
        auto it = r.features.find("future_ret");
        if (it != r.features.end() && !std::isnan(it->second)) sq += (it->second - mean_ret) * (it->second - mean_ret);
    }
    std_ret = std::sqrt(sq / ret_cnt);

    fmt::print("  面板大小: {} 行 x {} 个因子\n", panel.size(), feature_cols.size());
    fmt::print("  交易日数: {} ({} ~ {})\n", dates.size(), dates.front(), dates.back());
    fmt::print("  未来{}日收益率: 均值={:.3f}%, 标准差={:.2f}%\n",
               predict_horizon, mean_ret * 100.0, std_ret * 100.0);

    // Part 2: single-factor IC analysis
    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("第二部分: 单因子IC分析 (哪些因子最有预测力?)\n");
    fmt::print("{0}\n", std::string(80, '='));

    std::map<std::string, std::vector<double>> ic_series, ric_series;
    for (const auto& dt : dates) {
        std::vector<ml::PanelRow*> rows;
        for (auto& r : panel) if (r.date == dt) rows.push_back(&r);
        if (rows.size() < 10) continue;
        for (const auto& col : feature_cols) {
            std::vector<double> fvals, rets;
            for (const auto& pr : rows) {
                auto fit = pr->features.find(col);
                auto rit = pr->features.find("future_ret");
                if (fit != pr->features.end() && rit != pr->features.end() &&
                    !std::isnan(fit->second) && !std::isnan(rit->second)) {
                    fvals.push_back(fit->second);
                    rets.push_back(rit->second);
                }
            }
            if (fvals.size() >= 10) {
                double ic = pearson(fvals, rets);
                double ric = spearman(fvals, rets);
                if (!std::isnan(ic)) ic_series[col].push_back(ic);
                if (!std::isnan(ric)) ric_series[col].push_back(ric);
            }
        }
    }

    std::vector<FactorIC> results;
    for (const auto& col : feature_cols) {
        const auto& ics = ic_series[col];
        const auto& rics = ric_series[col];
        if (ics.size() < 30) continue;
        double ic_mean = std::accumulate(ics.begin(), ics.end(), 0.0) / ics.size();
        double ric_mean = std::accumulate(rics.begin(), rics.end(), 0.0) / rics.size();
        double ic_std = 0.0, ric_std = 0.0;
        for (double x : ics) ic_std += (x - ic_mean) * (x - ic_mean);
        for (double x : rics) ric_std += (x - ric_mean) * (x - ric_mean);
        ic_std = std::sqrt(ic_std / ics.size());
        ric_std = std::sqrt(ric_std / rics.size());
        size_t pos = 0;
        for (double x : ics) if (x > 0.0) ++pos;
        FactorIC f;
        f.factor = col;
        f.ic = ic_mean;
        f.icir = (ic_std > 0.0) ? ic_mean / ic_std : 0.0;
        f.rank_ic = ric_mean;
        f.rank_icir = (ric_std > 0.0) ? ric_mean / ric_std : 0.0;
        f.ic_positive = static_cast<double>(pos) / ics.size();
        f.n_days = ics.size();
        results.push_back(f);
    }

    std::sort(results.begin(), results.end(),
              [](const FactorIC& a, const FactorIC& b) { return std::abs(a.icir) > std::abs(b.icir); });

    fmt::print("\n因子IC排名 (Top 15, 按|ICIR|排序):\n");
    fmt::print("  {:<28s} {:>8s} {:>8s} {:>8s} {:>8s} {:>6s}\n",
               "因子", "IC", "ICIR", "RankIC", "RICIR", "IC>0");
    fmt::print("  {0}\n", std::string(75, '-'));
    for (size_t i = 0; i < 15 && i < results.size(); ++i) {
        fmt::print("  {:<28s} {:>8.4f} {:>8.4f} {:>8.4f} {:>8.4f} {:>6.1f}%\n",
                   results[i].factor, results[i].ic, results[i].icir,
                   results[i].rank_ic, results[i].rank_icir, results[i].ic_positive * 100.0);
    }

    auto taxonomy = ml::feature_taxonomy();
    fmt::print("\n各因子类别平均|ICIR|:\n");
    for (const auto& kv : taxonomy) {
        double sum = 0.0;
        size_t cnt = 0;
        std::string best_factor;
        double best_icir = 0.0;
        for (const auto& r : results) {
            if (std::find(kv.second.begin(), kv.second.end(), r.factor) != kv.second.end()) {
                sum += std::abs(r.icir); ++cnt;
                if (std::abs(r.icir) > std::abs(best_icir)) {
                    best_icir = r.icir;
                    best_factor = r.factor;
                }
            }
        }
        if (cnt > 0) {
            fmt::print("  {:<14s} 平均|ICIR|={:.3f}  最佳: {} (ICIR={:.3f})\n",
                       kv.first, sum / cnt, best_factor, best_icir);
        }
    }

    // Part 3: rolling cross-sectional prediction
    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("第三部分: 滚动RandomForest截面预测\n");
    fmt::print("{0}\n", std::string(80, '='));

    if (dates.size() < train_window + 20) {
        fmt::print("  [错误] 交易日数({})不足, 需要至少 {}\n", dates.size(), train_window + 20);
        return 1;
    }

    fmt::print("  训练窗口: {} 交易日\n", train_window);
    fmt::print("  预测目标: 未来{}日收益率\n", predict_horizon);
    fmt::print("  滚动步长: 每{}天预测一次\n", roll_step);

    std::vector<size_t> predict_indices;
    for (size_t i = train_window; i < dates.size(); i += roll_step) predict_indices.push_back(i);
    fmt::print("  预计预测: {} 次\n", predict_indices.size());

    // Cross-sectional preprocessing: per date MAD+zscore for feature_cols
    auto xs_panel = ml::preprocess_cross_section(panel, feature_cols);

    // Index panel by date for fast lookup
    std::map<std::string, std::vector<size_t>> date_idx;
    for (size_t i = 0; i < xs_panel.size(); ++i) date_idx[xs_panel[i].date].push_back(i);

    std::vector<double> daily_ics, daily_rics;
    ml::RandomForestClassifier model(30, 4, 10);
    t0 = std::chrono::steady_clock::now();

    for (size_t step = 0; step < predict_indices.size(); ++step) {
        size_t pred_idx = predict_indices[step];
        std::vector<std::string> train_dates(dates.begin() + pred_idx - train_window, dates.begin() + pred_idx);
        const std::string& pred_date = dates[pred_idx];

        std::vector<std::vector<double>> X_train;
        std::vector<double> y_train;
        for (const auto& d : train_dates) {
            auto it = date_idx.find(d);
            if (it == date_idx.end()) continue;
            for (size_t idx : it->second) {
                auto fit = xs_panel[idx].features.find("future_ret");
                if (fit == xs_panel[idx].features.end() || std::isnan(fit->second)) continue;
                std::vector<double> x;
                bool ok = true;
                for (const auto& c : feature_cols) {
                    auto cit = xs_panel[idx].features.find(c);
                    if (cit == xs_panel[idx].features.end() || std::isnan(cit->second)) { ok = false; break; }
                    x.push_back(cit->second);
                }
                if (!ok) continue;
                X_train.push_back(std::move(x));
                y_train.push_back(fit->second > 0.0 ? 1 : 0);
            }
        }

        auto pit = date_idx.find(pred_date);
        if (pit == date_idx.end()) continue;
        std::vector<std::vector<double>> X_test;
        std::vector<double> y_test;
        std::vector<size_t> test_idx;
        for (size_t idx : pit->second) {
            auto fit = xs_panel[idx].features.find("future_ret");
            if (fit == xs_panel[idx].features.end() || std::isnan(fit->second)) continue;
            std::vector<double> x;
            bool ok = true;
            for (const auto& c : feature_cols) {
                auto cit = xs_panel[idx].features.find(c);
                if (cit == xs_panel[idx].features.end() || std::isnan(cit->second)) { ok = false; break; }
                x.push_back(cit->second);
            }
            if (!ok) continue;
            X_test.push_back(std::move(x));
            y_test.push_back(fit->second);
            test_idx.push_back(idx);
        }

        if (X_test.size() < 5 || X_train.size() < 100) continue;

        bool has0 = false, has1 = false;
        for (int yi : y_train) { if (yi == 0) has0 = true; else has1 = true; }
        if (!has0 || !has1) continue;

        auto m = model;
        m.fit(X_train, y_train);

        std::vector<double> scores;
        for (const auto& x : X_test) scores.push_back(m.predict_proba(x));

        std::vector<double> valid_scores, valid_rets;
        for (size_t i = 0; i < y_test.size(); ++i) {
            if (!std::isnan(y_test[i])) {
                valid_scores.push_back(scores[i]);
                valid_rets.push_back(y_test[i]);
            }
        }
        if (valid_scores.size() < 5) continue;

        double ic = pearson(valid_scores, valid_rets);
        double ric = spearman(valid_scores, valid_rets);
        if (!std::isnan(ic)) daily_ics.push_back(ic);
        if (!std::isnan(ric)) daily_rics.push_back(ric);

        if ((step + 1) % 20 == 0) {
            t1 = std::chrono::steady_clock::now();
            double mean_ic = daily_ics.empty() ? 0.0 :
                std::accumulate(daily_ics.begin(), daily_ics.end(), 0.0) / daily_ics.size();
            fmt::print("  进度: {}/{} ({:.0f}s, 累计IC均值={:.4f})\n",
                       step + 1, predict_indices.size(),
                       std::chrono::duration<double>(t1 - t0).count(), mean_ic);
        }
    }
    t1 = std::chrono::steady_clock::now();
    fmt::print("  完成: {} 次有效预测, 耗时 {:.1f}s\n", daily_ics.size(),
               std::chrono::duration<double>(t1 - t0).count());

    if (daily_ics.empty()) {
        fmt::print("  [错误] 没有有效的预测结果\n");
        return 1;
    }

    double ic_mean = std::accumulate(daily_ics.begin(), daily_ics.end(), 0.0) / daily_ics.size();
    double ric_mean = std::accumulate(daily_rics.begin(), daily_rics.end(), 0.0) / daily_rics.size();
    double ic_std = 0.0, ric_std = 0.0;
    for (double x : daily_ics) ic_std += (x - ic_mean) * (x - ic_mean);
    for (double x : daily_rics) ric_std += (x - ric_mean) * (x - ric_mean);
    ic_std = std::sqrt(ic_std / daily_ics.size());
    ric_std = std::sqrt(ric_std / daily_rics.size());
    double icir = (ic_std > 0.0) ? ic_mean / ic_std : 0.0;
    double ricir = (ric_std > 0.0) ? ric_mean / ric_std : 0.0;
    size_t pos_ic = 0;
    for (double x : daily_ics) if (x > 0.0) ++pos_ic;
    double ic_positive = static_cast<double>(pos_ic) / daily_ics.size();

    fmt::print("\n--- RandomForest截面预测结果 ---\n");
    fmt::print("  IC:        {:.4f}\n", ic_mean);
    fmt::print("  ICIR:      {:.4f}\n", icir);
    fmt::print("  RankIC:    {:.4f}\n", ric_mean);
    fmt::print("  RankICIR:  {:.4f}\n", ricir);
    fmt::print("  IC>0占比:  {:.1f}% ({}/{})\n", ic_positive * 100.0, pos_ic, daily_ics.size());
    fmt::print("  IC最大值:  {:.4f}\n", *std::max_element(daily_ics.begin(), daily_ics.end()));
    fmt::print("  IC最小值:  {:.4f}\n", *std::min_element(daily_ics.begin(), daily_ics.end()));

    // Part 4: compare with MASTER
    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("第四部分: 与MASTER论文(CSI300)对比\n");
    fmt::print("{0}\n", std::string(80, '='));

    struct Range { double lo, hi; };
    std::map<std::string, Range> master_range = {
        {"IC", {0.050, 0.080}},
        {"ICIR", {0.400, 0.700}},
        {"RankIC", {0.080, 0.120}},
        {"RankICIR", {0.700, 1.100}},
    };
    std::map<std::string, double> our_metrics = {
        {"IC", ic_mean}, {"ICIR", icir}, {"RankIC", ric_mean}, {"RankICIR", ricir},
    };

    fmt::print("\n  {:<12s} {:>12s} {:>15s} {:>8s} {:>6s}\n",
               "指标", "我们(50只)", "MASTER(300只)", "差距", "评估");
    fmt::print("  {0}\n", std::string(60, '-'));
    for (const auto& key : {"IC", "ICIR", "RankIC", "RankICIR"}) {
        double ours = our_metrics[key];
        double m_lo = master_range[key].lo, m_hi = master_range[key].hi;
        double m_mid = (m_lo + m_hi) / 2.0;
        std::string assessment;
        if (std::abs(ours) >= m_lo) assessment = "达标";
        else if (std::abs(ours) >= m_lo * 0.6) assessment = "接近";
        else assessment = "差距大";
        double gap = std::abs(ours) - m_mid;
        fmt::print("  {:<12s} {:>12.4f} {:>7.3f}~{:>4.3f}      {:>+8.4f} {:>6s}\n",
                   key, ours, m_lo, m_hi, gap, assessment);
    }

    fmt::print(R"(
  条件差异分析:
    1. 股票数量: 50只 vs MASTER 300只
       -> 50只截面较小, IC的统计噪声更大
    2. 因子维度: 52维 vs MASTER 221维(158+63)
       -> MASTER覆盖了更多K线形态和回归因子
    3. 模型架构: RandomForest(截面独立) vs Transformer(双注意力)
       -> MASTER的S-Attention能捕捉板块联动, T-Attention捕捉多日模式
    4. 特征选择: 固定因子 vs Gate动态调整
       -> MASTER根据市场环境自动调整因子权重

  实践启示:
    - RandomForest + 50因子在A股上已经可产生有效IC信号
    - 要进一步提升, 可从三个方向:
      a) 扩大因子库 (加入Alpha158中我们缺少的因子)
      b) 引入市场状态特征 (类似Gate的63维市场信息)
      c) 升级模型架构 (Transformer + 时序/截面注意力)
)");

    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("[完成] 3-XGBoost截面预测.cpp 运行结束\n");

    return 0;
}
