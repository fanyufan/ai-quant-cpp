// 3-XGBoost涨跌预测
// 对应 week6/11-机器学习因子挖掘-20260321/3-XGBoost涨跌预测.py
// C++ 中使用自研 RandomForestClassifier 替代 XGBoost。

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
#include "ml_tree.hpp"

using namespace quant;

namespace {

struct StockResult {
    std::string code;
    std::string name;
    ml::ClfMetrics metrics;
    std::vector<ml::Prediction> preds;
    size_t n_samples = 0;
};

std::vector<ml::PanelRow> build_single_stock_panel(
    const std::vector<bt::Bar>& bars,
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

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    const std::map<std::string, std::string> XGB_STOCKS = {
        {"600519.SH", "贵州茅台"},
        {"688981.SH", "中芯国际"},
    };
    const std::string start_date = "2023-01-01";
    const std::string end_date = "2025-12-31";
    const size_t label_horizon = 1;
    const size_t train_window = 120;
    const size_t retrain_interval = 20;
    const int min_bars = 120;

    fmt::print("\n{0}\n", std::string(70, '='));
    std::string title = label_horizon == 1 ? "次日涨跌" : fmt::format("未来第{}日涨跌", label_horizon);
    fmt::print("  RandomForest {} 二分类预测模型\n", title);
    fmt::print("{0}\n", std::string(70, '='));
    fmt::print("  数据区间: {} ~ {}\n", start_date, end_date);
    fmt::print("  标的 ({}只): ", XGB_STOCKS.size());
    size_t idx = 0;
    for (const auto& kv : XGB_STOCKS) {
        if (idx++) fmt::print(", ");
        fmt::print("{}({})", kv.second, kv.first);
    }
    fmt::print("\n");
    fmt::print("  标签定义: 未来{}个交易日收盘 > 今日收盘 => 1(涨), 否则 => 0(跌)\n", label_horizon);
    fmt::print("  模型: RandomForest | 滚动窗口={}天 | 重训间隔={}天\n", train_window, retrain_interval);

    std::vector<StockResult> results;
    for (const auto& kv : XGB_STOCKS) {
        const auto& code = kv.first;
        const auto& name = kv.second;

        fmt::print("\n{0}\n", std::string(60, '='));
        fmt::print("  {} ({})\n", name, code);
        fmt::print("{0}\n", std::string(60, '='));

        // [1] load data
        fmt::print("\n[1] 加载数据: {} ~ {}\n", start_date, end_date);
        auto bars = bt::data::load_from_mysql(cfg, code, start_date, end_date);
        if (static_cast<int>(bars.size()) < min_bars) {
            fmt::print("  [错误] {} 有效交易日 {} < {}\n", code, bars.size(), min_bars);
            continue;
        }
        double mn = bars.front().close, mx = bars.front().close;
        for (const auto& b : bars) { mn = std::min(mn, b.close); mx = std::max(mx, b.close); }
        fmt::print("    共 {} 个交易日, 价格区间: {:.2f} ~ {:.2f}\n", bars.size(), mn, mx);

        // [2] features + preprocess
        fmt::print("[2] 计算技术特征 + 预处理 (MAD + Z-score)\n");
        auto features = ml::calc_features(bars);
        auto all_cols = ml::all_feature_names();
        std::vector<std::string> feature_cols;
        for (const auto& c : all_cols) {
            if (features.find(c) != features.end()) feature_cols.push_back(c);
        }
        auto panel = build_single_stock_panel(bars, features);
        auto preprocessed = ml::preprocess_panel(panel, feature_cols, "mad");
        fmt::print("    特征数量: {}\n", feature_cols.size());

        // [3] labels
        fmt::print("[3] 构建标签: 未来第{}日涨=1, 跌=0 (相对今日收盘)\n", label_horizon);
        std::vector<double> close;
        for (const auto& b : bars) close.push_back(b.close);
        auto labels = ml::make_binary_labels(close, label_horizon);
        size_t n_up = 0, n_down = 0;
        for (int l : labels) { if (l == 1) ++n_up; else if (l == 0) ++n_down; }
        fmt::print("    标签分布: 跌(0)={}, 涨(1)={}, 涨占比={:.1f}%\n",
                   n_down, n_up, 100.0 * n_up / (n_up + n_down));

        // Build X, y, dates aligned; skip rows where label or any feature is NaN
        std::vector<std::vector<double>> X;
        std::vector<int> y;
        std::vector<std::string> dates;
        for (size_t i = 0; i < preprocessed.size(); ++i) {
            if (labels[i] < 0) continue;
            std::vector<double> x;
            bool ok = true;
            for (const auto& c : feature_cols) {
                auto it = preprocessed[i].features.find(c);
                if (it == preprocessed[i].features.end() || std::isnan(it->second)) { ok = false; break; }
                x.push_back(it->second);
            }
            if (!ok) continue;
            X.push_back(std::move(x));
            y.push_back(labels[i]);
            dates.push_back(preprocessed[i].date);
        }
        if (X.size() < train_window + 10) {
            fmt::print("  [错误] 有效样本 {} 不足\n", X.size());
            continue;
        }

        // [4] rolling prediction
        fmt::print("[4] RandomForest 滚动预测 (train_days={}, retrain_interval={})\n",
                   train_window, retrain_interval);
        ml::RandomForestClassifier prototype(200, 5, 10);
        auto preds = ml::rolling_train_predict(X, y, dates, prototype, train_window, retrain_interval);
        fmt::print("    预测样本数: {}\n", preds.size());

        if (preds.empty()) {
            fmt::print("  [错误] 无预测结果\n");
            continue;
        }

        // [5] metrics
        std::vector<int> y_true, y_pred;
        std::vector<double> y_prob;
        for (const auto& p : preds) {
            y_true.push_back(p.y_true);
            y_pred.push_back(p.y_pred);
            y_prob.push_back(p.y_prob);
        }
        auto metrics = ml::evaluate_classification(y_true, y_pred, y_prob);
        fmt::print("\n[5] 整体评估指标\n");
        fmt::print("    AUC:       {:.4f}\n", metrics.auc);
        fmt::print("    Accuracy:  {:.4f}\n", metrics.accuracy);
        fmt::print("    Precision: {:.4f}\n", metrics.precision);
        fmt::print("    Recall:    {:.4f}\n", metrics.recall);
        fmt::print("    F1:        {:.4f}\n", metrics.f1);

        // [6] confusion matrix
        size_t tp = 0, fp = 0, tn = 0, fn = 0;
        for (size_t i = 0; i < y_true.size(); ++i) {
            if (y_true[i] == 1 && y_pred[i] == 1) ++tp;
            else if (y_true[i] == 0 && y_pred[i] == 1) ++fp;
            else if (y_true[i] == 0 && y_pred[i] == 0) ++tn;
            else if (y_true[i] == 1 && y_pred[i] == 0) ++fn;
        }
        fmt::print("\n[6] 混淆矩阵\n");
        fmt::print("              预测跌  预测涨\n");
        fmt::print("    实际跌    {:>5d}   {:>5d}\n", tn, fp);
        fmt::print("    实际涨    {:>5d}   {:>5d}\n", fn, tp);

        // [7] monthly accuracy
        fmt::print("\n[7] 按月准确率变化\n");
        std::map<std::string, std::vector<size_t>> monthly_idx;
        for (size_t i = 0; i < preds.size(); ++i) {
            std::string month = preds[i].date.substr(0, 7);
            monthly_idx[month].push_back(i);
        }
        fmt::print("    {:<12s} {:>6s} {:>10s} {:>8s}\n", "月份", "样本", "Accuracy", "AUC");
        fmt::print("    {0}\n", std::string(38, '-'));
        for (const auto& kv : monthly_idx) {
            std::vector<int> mt, mp;
            std::vector<double> mprob;
            for (size_t i : kv.second) {
                mt.push_back(y_true[i]);
                mp.push_back(y_pred[i]);
                mprob.push_back(y_prob[i]);
            }
            auto mm = ml::evaluate_classification(mt, mp, mprob);
            std::string auc_str = (mm.auc > 0.0) ? fmt::format("{:.4f}", mm.auc) : "  N/A ";
            fmt::print("    {:<12s} {:>6d} {:>10.4f} {:>8s}\n",
                       kv.first, kv.second.size(), mm.accuracy, auc_str);
        }

        results.push_back({code, name, metrics, preds, preds.size()});
    }

    // Compare stocks
    if (results.size() >= 2) {
        fmt::print("\n{0}\n", std::string(60, '='));
        fmt::print("  RandomForest 预测结果对比\n");
        fmt::print("{0}\n", std::string(60, '='));

        std::string header = "股票         AUC      Accuracy   Precision  Recall     F1       样本数";
        fmt::print("\n    {}\n", header);
        fmt::print("    {0}\n", std::string(header.size(), '-'));
        for (const auto& r : results) {
            fmt::print("    {:<12s} {:>8.4f} {:>10.4f} {:>10.4f} {:>8.4f} {:>8.4f} {:>8d}\n",
                       r.name, r.metrics.auc, r.metrics.accuracy, r.metrics.precision,
                       r.metrics.recall, r.metrics.f1, r.n_samples);
        }

        fmt::print("\n    月度准确率统计:\n");
        for (const auto& r : results) {
            std::map<std::string, std::vector<size_t>> monthly_idx;
            for (size_t i = 0; i < r.preds.size(); ++i) {
                monthly_idx[r.preds[i].date.substr(0, 7)].push_back(i);
            }
            std::vector<double> accs;
            for (const auto& kv : monthly_idx) {
                size_t correct = 0;
                for (size_t i : kv.second) {
                    if (r.preds[i].y_true == r.preds[i].y_pred) ++correct;
                }
                accs.push_back(static_cast<double>(correct) / kv.second.size());
            }
            if (!accs.empty()) {
                double mean_acc = std::accumulate(accs.begin(), accs.end(), 0.0) / accs.size();
                double mx = *std::max_element(accs.begin(), accs.end());
                double mn = *std::min_element(accs.begin(), accs.end());
                double sq = 0.0;
                for (double a : accs) sq += (a - mean_acc) * (a - mean_acc);
                double std_acc = std::sqrt(sq / accs.size());
                fmt::print("    {}: 均值={:.4f}, 标准差={:.4f}, 最高={:.4f}, 最低={:.4f}\n",
                           r.name, mean_acc, std_acc, mx, mn);
            }
        }

        // Predictability analysis
        fmt::print("\n{0}\n", std::string(60, '='));
        fmt::print("  可预测性分析\n");
        fmt::print("{0}\n", std::string(60, '='));
        fmt::print("  (可预测性的分析维度详见课件 Part1)\n");
        for (const auto& r : results) {
            std::map<std::string, std::vector<size_t>> monthly_idx;
            for (size_t i = 0; i < r.preds.size(); ++i) {
                monthly_idx[r.preds[i].date.substr(0, 7)].push_back(i);
            }
            std::vector<double> accs;
            for (const auto& kv : monthly_idx) {
                size_t correct = 0;
                for (size_t i : kv.second) {
                    if (r.preds[i].y_true == r.preds[i].y_pred) ++correct;
                }
                accs.push_back(static_cast<double>(correct) / kv.second.size());
            }
            double std_acc = 0.0;
            if (!accs.empty()) {
                double mean_acc = std::accumulate(accs.begin(), accs.end(), 0.0) / accs.size();
                double sq = 0.0;
                for (double a : accs) sq += (a - mean_acc) * (a - mean_acc);
                std_acc = std::sqrt(sq / accs.size());
            }
            std::string stability = (std_acc < 0.08) ? "稳定" : "波动较大";
            fmt::print("\n    {}:\n", r.name);
            fmt::print("      - 整体准确率: {:.4f}\n", r.metrics.accuracy);
            fmt::print("      - 月度准确率波动(std): {:.4f}\n", std_acc);
            fmt::print("      - 预测稳定性: {}\n", stability);
        }
    }

    // Conclusion
    if (!results.empty()) {
        double avg_acc = 0.0;
        for (const auto& r : results) avg_acc += r.metrics.accuracy;
        avg_acc /= results.size();
        fmt::print("\n{0}\n", std::string(60, '='));
        fmt::print("  结论\n");
        fmt::print("{0}\n", std::string(60, '='));
        std::string hz = label_horizon == 1 ? "次日" : fmt::format("未来第{}日", label_horizon);
        fmt::print("\n  RandomForest二分类预测模型完成，{}方向准确率约{:.0f}%，\n", hz, avg_acc * 100.0);
        fmt::print("  输出的概率值(0~1)就是'上涨概率因子'。\n");
        fmt::print("\n  该因子可作为多因子选股模型的alpha信号之一，\n");
        fmt::print("  与基本面因子、动量因子等组合使用，构建综合选股策略。\n");
    }
    fmt::print("\n");

    return 0;
}
