// 4-LightGBM对比与调参
// 对应 week6/11-机器学习因子挖掘-20260321/4-LightGBM对比与调参.py
// C++ 中比较 DecisionTree 与 RandomForest, 并做网格搜索调参。

#include <algorithm>
#include <chrono>
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
#include "ml_tree.hpp"

using namespace quant;

namespace {

struct PreparedData {
    std::vector<bt::Bar> bars;
    std::vector<std::string> feature_cols;
    std::vector<std::vector<double>> X;
    std::vector<int> y;
    std::vector<std::string> dates;
};

struct RollingResult {
    ml::ClfMetrics metrics;
    std::vector<ml::Prediction> preds;
    double elapsed = 0.0;
};

std::vector<ml::PanelRow> build_panel(const std::vector<bt::Bar>& bars,
                                      const std::map<std::string, std::vector<double>>& features) {
    std::vector<ml::PanelRow> panel;
    for (size_t i = 0; i < bars.size(); ++i) {
        ml::PanelRow row;
        row.date = bars[i].date;
        row.code = "X";
        row.close = bars[i].close;
        for (const auto& kv : features) row.features[kv.first] = kv.second[i];
        panel.push_back(std::move(row));
    }
    return panel;
}

PreparedData prepare_data(const quant::mysql::Config& cfg,
                          const std::string& code,
                          const std::string& start,
                          const std::string& end) {
    PreparedData out;
    out.bars = bt::data::load_from_mysql(cfg, code, start, end);
    if (out.bars.size() < 120) return out;

    auto features = ml::calc_features(out.bars);
    auto all_cols = ml::all_feature_names();
    for (const auto& c : all_cols) {
        if (features.find(c) != features.end()) out.feature_cols.push_back(c);
    }
    auto panel = build_panel(out.bars, features);
    auto preprocessed = ml::preprocess_panel(panel, out.feature_cols, "mad");

    std::vector<double> close;
    for (const auto& b : out.bars) close.push_back(b.close);
    auto labels = ml::make_binary_labels(close, 1);

    for (size_t i = 0; i < preprocessed.size(); ++i) {
        if (labels[i] < 0) continue;
        std::vector<double> x;
        bool ok = true;
        for (const auto& c : out.feature_cols) {
            auto it = preprocessed[i].features.find(c);
            if (it == preprocessed[i].features.end() || std::isnan(it->second)) { ok = false; break; }
            x.push_back(it->second);
        }
        if (!ok) continue;
        out.X.push_back(std::move(x));
        out.y.push_back(labels[i]);
        out.dates.push_back(preprocessed[i].date);
    }
    return out;
}

RollingResult run_rolling_rf(const std::vector<std::vector<double>>& X,
                             const std::vector<int>& y,
                             const std::vector<std::string>& dates,
                             const ml::RandomForestClassifier& prototype,
                             size_t train_window,
                             size_t retrain_interval) {
    RollingResult r;
    auto t0 = std::chrono::steady_clock::now();
    r.preds = ml::rolling_train_predict(X, y, dates, prototype, train_window, retrain_interval);
    auto t1 = std::chrono::steady_clock::now();
    r.elapsed = std::chrono::duration<double>(t1 - t0).count();

    if (!r.preds.empty()) {
        std::vector<int> yt, yp;
        std::vector<double> yprob;
        for (const auto& p : r.preds) {
            yt.push_back(p.y_true);
            yp.push_back(p.y_pred);
            yprob.push_back(p.y_prob);
        }
        r.metrics = ml::evaluate_classification(yt, yp, yprob);
    }
    return r;
}

RollingResult run_rolling_dt(const std::vector<std::vector<double>>& X,
                             const std::vector<int>& y,
                             const std::vector<std::string>& dates,
                             size_t train_window,
                             size_t retrain_interval) {
    RollingResult r;
    auto t0 = std::chrono::steady_clock::now();

    size_t n = X.size();
    if (n == 0 || y.size() != n || dates.size() != n || train_window == 0 || retrain_interval == 0) {
        auto t1 = std::chrono::steady_clock::now();
        r.elapsed = std::chrono::duration<double>(t1 - t0).count();
        return r;
    }

    ml::DecisionTree model(false, 5, 20);
    size_t last_train = static_cast<size_t>(-1);
    for (size_t i = train_window; i < n; ++i) {
        if (last_train == static_cast<size_t>(-1) || (i - last_train) >= retrain_interval) {
            size_t start = i - train_window;
            std::vector<std::vector<double>> Xtr(train_window);
            std::vector<double> ytr(train_window);
            for (size_t j = 0; j < train_window; ++j) {
                Xtr[j] = X[start + j];
                ytr[j] = static_cast<double>(y[start + j]);
            }
            bool has0 = false, has1 = false;
            for (double yi : ytr) { if (yi == 0.0) has0 = true; else if (yi == 1.0) has1 = true; }
            if (!has0 || !has1) continue;
            model = ml::DecisionTree(false, 5, 20);
            model.fit(Xtr, ytr);
            last_train = i;
        }
        ml::Prediction p;
        p.date = dates[i];
        p.y_true = y[i];
        p.y_prob = model.predict(X[i]);
        p.y_pred = p.y_prob > 0.5 ? 1 : 0;
        r.preds.push_back(p);
    }

    auto t1 = std::chrono::steady_clock::now();
    r.elapsed = std::chrono::duration<double>(t1 - t0).count();

    if (!r.preds.empty()) {
        std::vector<int> yt, yp;
        std::vector<double> yprob;
        for (const auto& p : r.preds) {
            yt.push_back(p.y_true);
            yp.push_back(p.y_pred);
            yprob.push_back(p.y_prob);
        }
        r.metrics = ml::evaluate_classification(yt, yp, yprob);
    }
    return r;
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    const std::map<std::string, std::string> STOCKS = {
        {"600519.SH", "贵州茅台"},
        {"688981.SH", "中芯国际"},
    };
    const std::string start_date = "2023-01-01";
    const std::string end_date = "2025-12-31";
    const size_t train_window = 120;
    const size_t retrain_interval = 20;

    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  RandomForest 对比实验与网格搜索调参\n");
    fmt::print("{0}\n", std::string(70, '='));
    fmt::print("  数据区间: {} ~ {}\n", start_date, end_date);
    fmt::print("  目标股票: ");
    size_t idx = 0;
    for (const auto& kv : STOCKS) {
        if (idx++) fmt::print(", ");
        fmt::print("{}({})", kv.second, kv.first);
    }
    fmt::print("\n");

    // Part 1: data preparation
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  [第一部分] 数据准备\n");
    fmt::print("{0}\n", std::string(70, '='));

    std::map<std::string, PreparedData> stock_data;
    for (const auto& kv : STOCKS) {
        fmt::print("\n  加载 {}({}) ...\n", kv.second, kv.first);
        auto pd = prepare_data(cfg, kv.first, start_date, end_date);
        if (pd.bars.size() < 120 || pd.X.empty()) {
            fmt::print("  [错误] {} 数据或有效样本不足\n", kv.first);
            continue;
        }
        fmt::print("    交易日: {}, 价格区间: {:.2f} ~ {:.2f}\n",
                   pd.bars.size(),
                   std::min_element(pd.bars.begin(), pd.bars.end(),
                                    [](const bt::Bar& a, const bt::Bar& b) { return a.close < b.close; })->close,
                   std::max_element(pd.bars.begin(), pd.bars.end(),
                                    [](const bt::Bar& a, const bt::Bar& b) { return a.close < b.close; })->close);
        stock_data[kv.first] = std::move(pd);
    }
    if (stock_data.empty()) {
        fmt::print("  没有可用数据, 程序退出\n");
        return 1;
    }

    // Part 2: RF default
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  [第二部分] RandomForest 默认参数 - 滚动预测\n");
    fmt::print("{0}\n", std::string(70, '='));

    ml::RandomForestClassifier rf_default(200, 5, 20);
    std::map<std::string, RollingResult> rf_default_results;
    for (const auto& kv : stock_data) {
        fmt::print("\n  --- {} ({}) ---\n", STOCKS.at(kv.first), kv.first);
        auto rr = run_rolling_rf(kv.second.X, kv.second.y, kv.second.dates,
                                 rf_default, train_window, retrain_interval);
        rf_default_results[kv.first] = rr;
        fmt::print("    AUC={:.4f}  Acc={:.4f}  F1={:.4f}  耗时={:.1f}s\n",
                   rr.metrics.auc, rr.metrics.accuracy, rr.metrics.f1, rr.elapsed);
    }

    // Part 3: DecisionTree baseline
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  [第三部分] DecisionTree 滚动预测 (对比基准)\n");
    fmt::print("{0}\n", std::string(70, '='));

    std::map<std::string, RollingResult> dt_results;
    for (const auto& kv : stock_data) {
        fmt::print("\n  --- {} ({}) ---\n", STOCKS.at(kv.first), kv.first);
        auto rr = run_rolling_dt(kv.second.X, kv.second.y, kv.second.dates,
                                 train_window, retrain_interval);
        dt_results[kv.first] = rr;
        fmt::print("    AUC={:.4f}  Acc={:.4f}  F1={:.4f}  耗时={:.1f}s\n",
                   rr.metrics.auc, rr.metrics.accuracy, rr.metrics.f1, rr.elapsed);
    }

    // Part 4: grid/random search on first stock
    std::string ref_code = stock_data.begin()->first;
    const auto& ref_info = stock_data[ref_code];
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  [第四部分] 网格搜索超参数优化 ({} {})\n", STOCKS.at(ref_code), ref_code);
    fmt::print("{0}\n", std::string(70, '='));
    fmt::print("  搜索空间: max_depth in {{3,5,7}}, min_samples_leaf in {{5,10,20}},\n");
    fmt::print("            max_features in {{sqrt, all}}\n");
    fmt::print("  目标: 最大化 Purged K-Fold AUC (n_splits=5, gap=5)\n");
    fmt::print("  组合数: {}\n", 3 * 3 * 2);

    struct Params {
        size_t max_depth;
        size_t min_samples_leaf;
        size_t max_features; // 0 means sqrt, n_features means all
    };
    std::vector<Params> param_grid;
    size_t n_features = ref_info.X.empty() ? 0 : ref_info.X[0].size();
    for (size_t md : {3, 5, 7}) {
        for (size_t msl : {5, 10, 20}) {
            param_grid.push_back({md, msl, 0});        // sqrt
            param_grid.push_back({md, msl, n_features}); // all
        }
    }

    double best_auc = -1.0;
    Params best_params = param_grid.empty() ? Params{5, 20, 0} : param_grid.front();
    auto t0 = std::chrono::steady_clock::now();
    for (const auto& p : param_grid) {
        ml::RandomForestClassifier proto(50, p.max_depth, p.min_samples_leaf, p.max_features, 42);
        auto cv = ml::purged_kfold_cv(ref_info.X, ref_info.y, proto, 5, 5);
        if (cv.empty()) continue;
        double mean_auc = 0.0;
        for (const auto& f : cv) mean_auc += f.metrics.auc;
        mean_auc /= cv.size();
        if (mean_auc > best_auc) {
            best_auc = mean_auc;
            best_params = p;
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    double search_time = std::chrono::duration<double>(t1 - t0).count();

    fmt::print("\n  搜索完成, 耗时 {:.1f}s\n", search_time);
    fmt::print("  最优 CV AUC: {:.4f}\n", best_auc);
    fmt::print("  最优参数:\n");
    fmt::print("    {:<22s} = {}\n", "max_depth", best_params.max_depth);
    fmt::print("    {:<22s} = {}\n", "min_samples_leaf", best_params.min_samples_leaf);
    fmt::print("    {:<22s} = {}\n", "max_features",
               best_params.max_features == 0 ? "sqrt" : "all");

    // Part 5: re-run with best params on both stocks
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  [第五部分] 最优参数 RandomForest - 滚动预测\n");
    fmt::print("{0}\n", std::string(70, '='));

    ml::RandomForestClassifier rf_tuned(200, best_params.max_depth,
                                        best_params.min_samples_leaf, best_params.max_features, 42);
    std::map<std::string, RollingResult> rf_tuned_results;
    for (const auto& kv : stock_data) {
        fmt::print("\n  --- {} ({}) ---\n", STOCKS.at(kv.first), kv.first);
        auto rr = run_rolling_rf(kv.second.X, kv.second.y, kv.second.dates,
                                 rf_tuned, train_window, retrain_interval);
        rf_tuned_results[kv.first] = rr;
        fmt::print("    AUC={:.4f}  Acc={:.4f}  F1={:.4f}  耗时={:.1f}s\n",
                   rr.metrics.auc, rr.metrics.accuracy, rr.metrics.f1, rr.elapsed);
    }

    // Part 6: Purged K-Fold CV details
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  [第六部分] Purged K-Fold 交叉验证 (n_splits=5, gap=5)\n");
    fmt::print("{0}\n", std::string(70, '='));

    for (const auto& kv : stock_data) {
        fmt::print("\n  --- {} ({}) ---\n", STOCKS.at(kv.first), kv.first);
        auto cv = ml::purged_kfold_cv(kv.second.X, kv.second.y, rf_tuned, 5, 5);
        fmt::print("    {:>6s} {:>8s} {:>10s} {:>10s} {:>8s} {:>8s}\n",
                   "Fold", "AUC", "Accuracy", "Precision", "Recall", "F1");
        fmt::print("    {0}\n", std::string(52, '-'));
        double avg_auc = 0.0, avg_acc = 0.0, avg_f1 = 0.0;
        for (const auto& fr : cv) {
            fmt::print("    {:>6d} {:>8.4f} {:>10.4f} {:>10.4f} {:>8.4f} {:>8.4f}\n",
                       fr.fold, fr.metrics.auc, fr.metrics.accuracy,
                       fr.metrics.precision, fr.metrics.recall, fr.metrics.f1);
            avg_auc += fr.metrics.auc;
            avg_acc += fr.metrics.accuracy;
            avg_f1 += fr.metrics.f1;
        }
        if (!cv.empty()) {
            avg_auc /= cv.size(); avg_acc /= cv.size(); avg_f1 /= cv.size();
            fmt::print("    {:>6s} {:>8.4f} {:>10.4f} {:>10s} {:>8s} {:>8.4f}\n",
                       "均值", avg_auc, avg_acc, "", "", avg_f1);
        }
    }

    // Part 7: compare table
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  [第七部分] DecisionTree vs RandomForest 性能对比\n");
    fmt::print("{0}\n", std::string(70, '='));

    fmt::print("\n  {:<10s} {:<18s} {:>8s} {:>10s} {:>8s} {:>12s}\n",
               "股票", "模型", "AUC", "Accuracy", "F1", "训练时间(s)");
    fmt::print("  {0}\n", std::string(68, '-'));

    double avg_dt_auc = 0.0, avg_dt_time = 0.0;
    double avg_rf_d_auc = 0.0, avg_rf_d_time = 0.0;
    double avg_rf_t_auc = 0.0, avg_rf_t_time = 0.0;
    int cnt = 0;
    for (const auto& kv : stock_data) {
        const auto& name = STOCKS.at(kv.first);
        const auto& dt = dt_results[kv.first];
        const auto& rfd = rf_default_results[kv.first];
        const auto& rft = rf_tuned_results[kv.first];

        fmt::print("  {:<10s} {:<18s} {:>8.4f} {:>10.4f} {:>8.4f} {:>12.1f}\n",
                   name, "DecisionTree", dt.metrics.auc, dt.metrics.accuracy,
                   dt.metrics.f1, dt.elapsed);
        fmt::print("  {:<10s} {:<18s} {:>8.4f} {:>10.4f} {:>8.4f} {:>12.1f}\n",
                   "", "RF(默认)", rfd.metrics.auc, rfd.metrics.accuracy,
                   rfd.metrics.f1, rfd.elapsed);
        fmt::print("  {:<10s} {:<18s} {:>8.4f} {:>10.4f} {:>8.4f} {:>12.1f}\n\n",
                   "", "RF(调优)", rft.metrics.auc, rft.metrics.accuracy,
                   rft.metrics.f1, rft.elapsed);

        avg_dt_auc += dt.metrics.auc; avg_dt_time += dt.elapsed;
        avg_rf_d_auc += rfd.metrics.auc; avg_rf_d_time += rfd.elapsed;
        avg_rf_t_auc += rft.metrics.auc; avg_rf_t_time += rft.elapsed;
        ++cnt;
    }
    if (cnt > 0) {
        avg_dt_auc /= cnt; avg_dt_time /= cnt;
        avg_rf_d_auc /= cnt; avg_rf_d_time /= cnt;
        avg_rf_t_auc /= cnt; avg_rf_t_time /= cnt;
    }

    fmt::print("  平均 AUC 对比:\n");
    fmt::print("    DecisionTree:    {:.4f}  (平均耗时 {:.1f}s)\n", avg_dt_auc, avg_dt_time);
    fmt::print("    RF(默认):        {:.4f}  (平均耗时 {:.1f}s)\n", avg_rf_d_auc, avg_rf_d_time);
    fmt::print("    RF(调优):        {:.4f}  (平均耗时 {:.1f}s)\n", avg_rf_t_auc, avg_rf_t_time);

    double speed_ratio = (avg_dt_time > 0.0) ? avg_dt_time / avg_rf_d_time : 0.0;
    fmt::print("\n  训练速度: RF(默认) 比 DecisionTree 快约 {:.1f} 倍\n", speed_ratio);

    // Summary
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  分析洞察\n");
    fmt::print("{0}\n", std::string(70, '='));
    fmt::print("\n  网格搜索调优后 RF AUC: {:.4f} -> {:.4f}\n", avg_rf_d_auc, avg_rf_t_auc);
    fmt::print("  RF(默认) 训练速度约为 DecisionTree 的 {:.1f} 倍\n", speed_ratio);

    return 0;
}
