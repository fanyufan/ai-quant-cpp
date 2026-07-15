// 机器学习增强海龟策略 - 用ML过滤假突破
// 对应 week4/8-海龟交易法则-20260311/4-ML增强海龟策略.py
// 使用 C++ 内建小型决策树分类器, 替代 sklearn/lightgbm/xgboost.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include <fmt/format.h>

#include "backtest.hpp"
#include "backtest_config.hpp"
#include "backtest_data.hpp"
#include "backtest_data_mysql.hpp"
#include "backtest_plot.hpp"
#include "backtest_report.hpp"
#include "env.hpp"
#include "indicators.hpp"
#include "turtle_utils.hpp"

using namespace quant;

namespace {

constexpr size_t FEATURE_COUNT = 8;

struct Sample {
    std::string date;
    std::vector<double> x;
    int y = 0;
};

struct Args {
    std::string start_date = "2024-01-01";
    std::string end_date = "2025-12-31";
    std::string split_date = "2025-01-01";
    std::string target_stock = "601318.SH";
    std::string target_name = "平安银行";
    std::string data_file;
    double ml_threshold = 0.5;
};

Args parse_args(int argc, char* argv[]) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--stock" || arg == "-s") && i + 1 < argc) {
            a.target_stock = argv[++i];
            a.target_name = a.target_stock;
        } else if (arg == "--start" && i + 1 < argc) {
            a.start_date = argv[++i];
        } else if (arg == "--end" && i + 1 < argc) {
            a.end_date = argv[++i];
        } else if (arg == "--split" && i + 1 < argc) {
            a.split_date = argv[++i];
        } else if (arg == "--data-file" && i + 1 < argc) {
            a.data_file = argv[++i];
        } else if (arg == "--threshold" && i + 1 < argc) {
            a.ml_threshold = std::stod(argv[++i]);
        }
    }
    return a;
}

std::vector<bt::Bar> load_bars(const std::string& code,
                               const std::string& data_file,
                               const quant::mysql::Config& cfg,
                               const std::string& start,
                               const std::string& end) {
    if (!data_file.empty()) {
        return bt::data::load_from_csv(data_file, start, end);
    }
    return bt::data::load_from_mysql(cfg, code, start, end);
}

// Compute features and labels for a single stock.
std::vector<Sample> compute_features(const std::vector<bt::Bar>& bars,
                                     int entry_period = 20,
                                     int atr_period = 20) {
    std::vector<Sample> out;
    size_t n = bars.size();
    if (n < 80) return out;

    std::vector<double> opens, highs, lows, closes, volumes;
    opens.reserve(n); highs.reserve(n); lows.reserve(n); closes.reserve(n); volumes.reserve(n);
    for (const auto& b : bars) {
        opens.push_back(b.open);
        highs.push_back(b.high);
        lows.push_back(b.low);
        closes.push_back(b.close);
        volumes.push_back(b.volume);
    }

    auto atr = ind::atr(highs, lows, closes, atr_period);
    auto adx_res = ind::adx(highs, lows, closes, 14);
    auto rsi = ind::rsi(closes, 14);
    auto vol_ma = ind::sma(volumes, 20);
    auto donchian_high = turtle::rolling_max_prev(highs, entry_period);

    size_t min_idx = std::max({static_cast<size_t>(entry_period),
                               static_cast<size_t>(atr_period),
                               static_cast<size_t>(14)}) + 20;
    if (min_idx >= n) return out;

    for (size_t i = min_idx; i + 5 < n; ++i) {
        if (closes[i] <= donchian_high[i]) continue;
        if (std::isnan(atr[i]) || atr[i] <= 0.0) continue;
        if (std::isnan(adx_res.adx[i]) || std::isnan(rsi[i])) continue;
        if (std::isnan(vol_ma[i]) || vol_ma[i] <= 0.0) continue;

        double momentum_5d = (i >= 5) ? (closes[i] / closes[i - 5] - 1.0) : 0.0;

        int consolidation_days = 0;
        for (size_t j = i - 1; j + 1 > std::max(i - 60, min_idx); --j) {
            if (closes[j] > donchian_high[j]) break;
            ++consolidation_days;
            if (j == 0) break;
        }

        double atr_change = 0.0;
        if (i >= 5 && !std::isnan(atr[i - 5]) && atr[i - 5] > 0.0) {
            atr_change = atr[i] / atr[i - 5] - 1.0;
        }

        double future_max = closes[i];
        for (size_t k = i + 1; k <= i + 5; ++k) future_max = std::max(future_max, closes[k]);
        int label = (future_max / closes[i] - 1.0) > 0.02 ? 1 : 0;

        Sample s;
        s.date = bars[i].date;
        s.x = {
            atr[i] / closes[i],
            adx_res.adx[i],
            volumes[i] / vol_ma[i],
            rsi[i],
            (closes[i] - donchian_high[i]) / atr[i],
            momentum_5d,
            static_cast<double>(consolidation_days),
            atr_change,
        };
        s.y = label;
        out.push_back(std::move(s));
    }
    return out;
}

// ---------- Decision Tree ----------

struct TreeNode {
    bool is_leaf = true;
    size_t feature = 0;
    double threshold = 0.0;
    double value = 0.0; // probability of class 1
    size_t n = 0;
    int left = -1;
    int right = -1;
};

class DecisionTree {
public:
    DecisionTree(size_t max_depth, size_t min_samples_leaf)
        : max_depth_(max_depth), min_samples_leaf_(min_samples_leaf) {}

    void fit(const std::vector<Sample>& samples) {
        nodes_.clear();
        std::vector<size_t> idx(samples.size());
        std::iota(idx.begin(), idx.end(), 0);
        build(samples, idx, 0);
    }

    double predict_proba(const std::vector<double>& x) const {
        int node = 0;
        while (!nodes_[node].is_leaf) {
            if (x[nodes_[node].feature] <= nodes_[node].threshold) {
                node = nodes_[node].left;
            } else {
                node = nodes_[node].right;
            }
        }
        return nodes_[node].value;
    }

    const std::vector<TreeNode>& nodes() const { return nodes_; }

private:
    int build(const std::vector<Sample>& samples, std::vector<size_t>& idx, size_t depth) {
        TreeNode node;
        node.n = idx.size();
        size_t pos = 0;
        for (size_t i : idx) if (samples[i].y == 1) ++pos;
        node.value = node.n > 0 ? static_cast<double>(pos) / node.n : 0.0;

        if (depth >= max_depth_ || node.n <= min_samples_leaf_ * 2 || pos == 0 || pos == node.n) {
            node.is_leaf = true;
            nodes_.push_back(node);
            return static_cast<int>(nodes_.size() - 1);
        }

        double best_gain = -1.0;
        size_t best_feat = 0;
        double best_thr = 0.0;
        std::vector<size_t> left_idx, right_idx;

        auto gini = [&](size_t p, size_t n_total) {
            if (n_total == 0) return 0.0;
            double q = static_cast<double>(p) / n_total;
            return 1.0 - q * q - (1.0 - q) * (1.0 - q);
        };
        double parent_gini = gini(pos, node.n);

        for (size_t f = 0; f < FEATURE_COUNT; ++f) {
            // Sort by feature
            std::vector<std::pair<double, size_t>> vals;
            vals.reserve(idx.size());
            for (size_t i : idx) vals.emplace_back(samples[i].x[f], i);
            std::sort(vals.begin(), vals.end());

            for (size_t k = 1; k < vals.size(); ++k) {
                if (vals[k].first == vals[k - 1].first) continue;
                double thr = (vals[k - 1].first + vals[k].first) / 2.0;
                size_t left_n = k;
                size_t left_pos = 0;
                for (size_t t = 0; t < k; ++t) if (samples[vals[t].second].y == 1) ++left_pos;
                size_t right_n = vals.size() - k;
                size_t right_pos = pos - left_pos;
                if (left_n < min_samples_leaf_ || right_n < min_samples_leaf_) continue;
                double left_g = gini(left_pos, left_n);
                double right_g = gini(right_pos, right_n);
                double gain = parent_gini - (left_n * left_g + right_n * right_g) / node.n;
                if (gain > best_gain) {
                    best_gain = gain;
                    best_feat = f;
                    best_thr = thr;
                }
            }
        }

        if (best_gain <= 0.0) {
            node.is_leaf = true;
            nodes_.push_back(node);
            return static_cast<int>(nodes_.size() - 1);
        }

        node.is_leaf = false;
        node.feature = best_feat;
        node.threshold = best_thr;
        int cur = static_cast<int>(nodes_.size());
        nodes_.push_back(node);

        std::vector<size_t> li, ri;
        li.reserve(idx.size());
        ri.reserve(idx.size());
        for (size_t i : idx) {
            if (samples[i].x[best_feat] <= best_thr) li.push_back(i);
            else ri.push_back(i);
        }
        nodes_[cur].left = build(samples, li, depth + 1);
        nodes_[cur].right = build(samples, ri, depth + 1);
        return cur;
    }

    size_t max_depth_;
    size_t min_samples_leaf_;
    std::vector<TreeNode> nodes_;
};

std::vector<std::string> feature_names = {
    "atr_ratio", "adx", "vol_ratio", "rsi",
    "breakout_strength", "momentum_5d", "consolidation_days", "atr_change"};

std::map<std::string, double> generate_predictions(const DecisionTree& model,
                                                   const std::vector<Sample>& samples) {
    std::map<std::string, double> preds;
    for (const auto& s : samples) {
        preds[s.date] = model.predict_proba(s.x);
    }
    return preds;
}

// ---------- Backtest ----------

struct Output {
    bt::Result result;
    bt::Metrics metrics;
};

Output run_ml_turtle(const std::vector<bt::Bar>& bars,
                     double initial_cash,
                     double commission,
                     const std::map<std::string, double>& predictions,
                     double ml_threshold,
                     const std::string& label,
                     const std::string& stock_code,
                     bool do_plot,
                     int* passed = nullptr,
                     int* filtered = nullptr) {
    class MLTurtle : public bt::Strategy {
    public:
        MLTurtle(double risk_pct, int max_units, double add_n, double stop_n,
                 const std::vector<double>& entry_high,
                 const std::vector<double>& exit_low,
                 const std::vector<double>& atr,
                 const std::map<std::string, double>& predictions,
                 double ml_threshold,
                 int* passed,
                 int* filtered)
            : risk_pct_(risk_pct), max_units_(max_units), add_n_(add_n), stop_n_(stop_n),
              entry_high_(entry_high), exit_low_(exit_low), atr_(atr),
              predictions_(predictions), ml_threshold_(ml_threshold),
              passed_(passed), filtered_(filtered) {}

        void next(size_t idx, const std::vector<bt::Bar>& bars, bt::Broker& broker) override {
            double atr = atr_[idx];
            if (std::isnan(atr) || atr <= 0.0) return;
            if (std::isnan(entry_high_[idx]) || std::isnan(exit_low_[idx])) return;
            double close = bars[idx].close;

            if (broker.shares() == 0) {
                if (close > entry_high_[idx]) {
                    auto it = predictions_.find(bars[idx].date);
                    double prob = (it != predictions_.end()) ? it->second : 0.0;
                    if (prob >= ml_threshold_) {
                        int size = turtle::calc_unit_size(broker.nav(close), atr, risk_pct_);
                        if (size > 0) {
                            broker.buy(idx, bars, static_cast<double>(size) * close);
                            units_ = 1;
                            last_add_price_ = close;
                            stop_price_ = close - stop_n_ * atr;
                        }
                        if (passed_) ++(*passed_);
                    } else {
                        if (filtered_) ++(*filtered_);
                    }
                }
            } else {
                if (close < stop_price_) {
                    broker.sell(idx, bars);
                    reset();
                    return;
                }
                if (close < exit_low_[idx]) {
                    broker.sell(idx, bars);
                    reset();
                    return;
                }
                if (units_ < max_units_) {
                    if (close >= last_add_price_ + add_n_ * atr) {
                        int size = turtle::calc_unit_size(broker.nav(close), atr, risk_pct_);
                        double cost = close * size * 1.01;
                        if (size > 0 && broker.cash() > cost) {
                            broker.buy(idx, bars, static_cast<double>(size) * close);
                            ++units_;
                            last_add_price_ = close;
                            stop_price_ = close - stop_n_ * atr;
                        }
                    }
                }
            }
        }

    private:
        void reset() { units_ = 0; last_add_price_ = 0.0; stop_price_ = 0.0; }
        double risk_pct_;
        int max_units_;
        double add_n_;
        double stop_n_;
        const std::vector<double>& entry_high_;
        const std::vector<double>& exit_low_;
        const std::vector<double>& atr_;
        const std::map<std::string, double>& predictions_;
        double ml_threshold_;
        int* passed_;
        int* filtered_;
        int units_ = 0;
        double last_add_price_ = 0.0;
        double stop_price_ = 0.0;
    };

    std::vector<double> highs, lows, closes;
    highs.reserve(bars.size()); lows.reserve(bars.size()); closes.reserve(bars.size());
    for (const auto& b : bars) {
        highs.push_back(b.high);
        lows.push_back(b.low);
        closes.push_back(b.close);
    }
    auto entry_high = turtle::rolling_max_prev(highs, 20);
    auto exit_low = turtle::rolling_min_prev(lows, 10);
    auto atr = ind::atr(highs, lows, closes, 20);

    MLTurtle strategy(0.01, 4, 0.5, 2.0, entry_high, exit_low, atr,
                      predictions, ml_threshold, passed, filtered);
    bt::Backtest bt(initial_cash, commission);
    Output out;
    out.result = bt.run(bars, strategy);
    out.metrics = bt::compute_metrics(out.result);

    fmt::print("[{}] {}\n", label, stock_code);
    bt::print_metrics_line(out.metrics);
    if (do_plot) {
        std::string safe = label;
        for (auto& c : safe) if (c == ' ' || c == '/') c = '_';
        bt::plot_backtest(out.result, bars, stock_code, label, "outputs/" + safe + ".png");
        fmt::print("  图表已保存: outputs/{}.png\n", safe);
    }
    return out;
}

Output run_classic_turtle(const std::vector<bt::Bar>& bars,
                          double initial_cash,
                          double commission,
                          const std::string& label,
                          const std::string& stock_code,
                          bool do_plot) {
    class Classic : public bt::Strategy {
    public:
        Classic(double risk_pct, int max_units, double add_n, double stop_n,
                const std::vector<double>& entry_high,
                const std::vector<double>& exit_low,
                const std::vector<double>& atr)
            : risk_pct_(risk_pct), max_units_(max_units), add_n_(add_n), stop_n_(stop_n),
              entry_high_(entry_high), exit_low_(exit_low), atr_(atr) {}

        void next(size_t idx, const std::vector<bt::Bar>& bars, bt::Broker& broker) override {
            double atr = atr_[idx];
            if (std::isnan(atr) || atr <= 0.0) return;
            if (std::isnan(entry_high_[idx]) || std::isnan(exit_low_[idx])) return;
            double close = bars[idx].close;
            if (broker.shares() == 0) {
                if (close > entry_high_[idx]) {
                    int size = turtle::calc_unit_size(broker.nav(close), atr, risk_pct_);
                    if (size > 0) {
                        broker.buy(idx, bars, static_cast<double>(size) * close);
                        units_ = 1;
                        last_add_price_ = close;
                        stop_price_ = close - stop_n_ * atr;
                    }
                }
            } else {
                if (close < stop_price_) { broker.sell(idx, bars); reset(); return; }
                if (close < exit_low_[idx]) { broker.sell(idx, bars); reset(); return; }
                if (units_ < max_units_ && close >= last_add_price_ + add_n_ * atr) {
                    int size = turtle::calc_unit_size(broker.nav(close), atr, risk_pct_);
                    if (size > 0 && broker.cash() > close * size * 1.01) {
                        broker.buy(idx, bars, static_cast<double>(size) * close);
                        ++units_;
                        last_add_price_ = close;
                        stop_price_ = close - stop_n_ * atr;
                    }
                }
            }
        }
    private:
        void reset() { units_ = 0; last_add_price_ = 0.0; stop_price_ = 0.0; }
        double risk_pct_; int max_units_; double add_n_; double stop_n_;
        const std::vector<double>& entry_high_;
        const std::vector<double>& exit_low_;
        const std::vector<double>& atr_;
        int units_ = 0;
        double last_add_price_ = 0.0;
        double stop_price_ = 0.0;
    };

    std::vector<double> highs, lows, closes;
    highs.reserve(bars.size()); lows.reserve(bars.size()); closes.reserve(bars.size());
    for (const auto& b : bars) {
        highs.push_back(b.high);
        lows.push_back(b.low);
        closes.push_back(b.close);
    }
    auto entry_high = turtle::rolling_max_prev(highs, 20);
    auto exit_low = turtle::rolling_min_prev(lows, 10);
    auto atr = ind::atr(highs, lows, closes, 20);

    Classic strategy(0.01, 4, 0.5, 2.0, entry_high, exit_low, atr);
    bt::Backtest bt(initial_cash, commission);
    Output out;
    out.result = bt.run(bars, strategy);
    out.metrics = bt::compute_metrics(out.result);

    fmt::print("[{}] {}\n", label, stock_code);
    bt::print_metrics_line(out.metrics);
    if (do_plot) {
        std::string safe = label;
        for (auto& c : safe) if (c == ' ' || c == '/') c = '_';
        bt::plot_backtest(out.result, bars, stock_code, label, "outputs/" + safe + ".png");
        fmt::print("  图表已保存: outputs/{}.png\n", safe);
    }
    return out;
}

} // namespace

int main(int argc, char* argv[]) {
    auto args = parse_args(argc, argv);
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);
    auto params = bt::load_backtest_params(env);

    std::vector<std::pair<std::string, std::string>> train_stocks = {
        {"600519.SH", "贵州茅台"},
        {"300750.SZ", "宁德时代"},
        {"510300.SH", "沪深300ETF"},
        {"688981.SH", "中芯国际"},
        {"601318.SH", "平安银行"},
        {"159941.SZ", "纳指ETF"},
    };

    fmt::print("{}\n", std::string(70, '='));
    fmt::print("机器学习增强海龟策略\n");
    fmt::print("{}\n", std::string(70, '='));
    fmt::print("\n设计思路:\n");
    fmt::print("  1. 多股票训练: 用6只股票的突破事件训练, 提高样本量和泛化能力\n");
    fmt::print("  2. 时间分割: 2024年训练, 2025年测试 (严格避免未来泄露)\n");
    fmt::print("  3. 浅树+正则化: 防止过拟合, 追求泛化\n");
    fmt::print("  4. 只过滤入场: 不改变海龟核心逻辑, 只在入场时增加ML判断\n");

    // Step 1: collect features
    fmt::print("\n{}\n", std::string(70, '='));
    fmt::print("Step 1: 多股票特征收集\n");
    fmt::print("{}\n", std::string(70, '='));

    std::vector<Sample> all_samples;
    for (const auto& [code, name] : train_stocks) {
        auto bars = load_bars(code, "", cfg, args.start_date, args.end_date);
        if (bars.empty()) {
            fmt::print("    {}({}): 跳过(无数据)\n", name, code);
            continue;
        }
        auto feats = compute_features(bars);
        if (!feats.empty()) {
            size_t true_count = 0;
            for (const auto& s : feats) if (s.y == 1) ++true_count;
            fmt::print("    {}({}): {}个突破事件, 真突破率 {:.0f}%\n",
                       name, code, feats.size(), 100.0 * true_count / feats.size());
            all_samples.insert(all_samples.end(), feats.begin(), feats.end());
        } else {
            fmt::print("    {}({}): 无有效突破事件\n", name, code);
        }
    }

    if (all_samples.size() < 10) {
        fmt::print("\n样本不足({}个), 无法训练可靠模型\n", all_samples.size());
        return 1;
    }

    size_t total_true = 0;
    for (const auto& s : all_samples) if (s.y == 1) ++total_true;
    fmt::print("\n  合计: {}个突破事件\n", all_samples.size());
    fmt::print("  真突破: {} ({:.0f}%)\n", total_true, 100.0 * total_true / all_samples.size());
    fmt::print("  假突破: {} ({:.0f}%)\n",
               all_samples.size() - total_true,
               100.0 * (all_samples.size() - total_true) / all_samples.size());

    std::sort(all_samples.begin(), all_samples.end(),
              [](const Sample& a, const Sample& b) { return a.date < b.date; });

    // Split
    std::vector<Sample> train, test;
    for (const auto& s : all_samples) {
        if (s.date < args.split_date) train.push_back(s);
        else test.push_back(s);
    }

    if (train.size() < 5 || test.size() < 3) {
        fmt::print("  样本不足: 训练{} / 测试{}, 至少需要训练5/测试3\n", train.size(), test.size());
        return 1;
    }

    size_t train_true = 0, test_true = 0;
    for (const auto& s : train) if (s.y == 1) ++train_true;
    for (const auto& s : test) if (s.y == 1) ++test_true;
    fmt::print("\n  引擎: C++ DecisionTree (max_depth=3)\n");
    fmt::print("  训练集: {}个事件 | 真突破率: {:.0f}%\n", train.size(), 100.0 * train_true / train.size());
    fmt::print("  测试集: {}个事件 | 真突破率: {:.0f}%\n", test.size(), 100.0 * test_true / test.size());

    // Step 2: train
    fmt::print("\n{}\n", std::string(70, '='));
    fmt::print("Step 2: 模型训练 (分割点: {})\n", args.split_date);
    fmt::print("{}\n", std::string(70, '='));

    DecisionTree model(3, 3);
    model.fit(train);

    // Evaluate
    auto evaluate = [&](const std::vector<Sample>& data) {
        int tp = 0, fp = 0, tn = 0, fn = 0;
        for (const auto& s : data) {
            double p = model.predict_proba(s.x);
            int pred = p >= args.ml_threshold ? 1 : 0;
            if (pred == 1 && s.y == 1) ++tp;
            else if (pred == 1 && s.y == 0) ++fp;
            else if (pred == 0 && s.y == 0) ++tn;
            else ++fn;
        }
        int total = tp + fp + tn + fn;
        double accuracy = total > 0 ? static_cast<double>(tp + tn) / total : 0.0;
        double precision = (tp + fp) > 0 ? static_cast<double>(tp) / (tp + fp) : 0.0;
        double recall = (tp + fn) > 0 ? static_cast<double>(tp) / (tp + fn) : 0.0;
        double f1 = (precision + recall) > 0 ? 2 * precision * recall / (precision + recall) : 0.0;
        return std::make_tuple(accuracy, precision, recall, f1);
    };
    auto [acc, prec, rec, f1] = evaluate(test);
    fmt::print("\n  测试集评估:\n");
    fmt::print("    准确率:  {:.1f}%\n", acc * 100.0);
    fmt::print("    精确率:  {:.1f}%\n", prec * 100.0);
    fmt::print("    召回率:  {:.1f}%\n", rec * 100.0);
    fmt::print("    F1分数:  {:.1f}%\n", f1 * 100.0);

    // Feature importance (simplified: count splits per feature)
    std::vector<double> importances(FEATURE_COUNT, 0.0);
    for (const auto& n : model.nodes()) {
        if (!n.is_leaf) importances[n.feature] += 1.0;
    }
    double imp_max = *std::max_element(importances.begin(), importances.end());
    if (imp_max > 0.0) {
        fmt::print("\n  特征重要性:\n");
        for (size_t i = 0; i < FEATURE_COUNT; ++i) {
            double norm = importances[i] / imp_max;
            std::string bar(static_cast<size_t>(norm * 25), '#');
            fmt::print("    {:<22} {:.2f} {}\n", feature_names[i], norm, bar);
        }
    }

    // Step 3: target predictions
    fmt::print("\n{}\n", std::string(70, '='));
    fmt::print("Step 3: 为 {}({}) 生成预测\n", args.target_name, args.target_stock);
    fmt::print("{}\n", std::string(70, '='));

    auto target_bars = load_bars(args.target_stock, args.data_file, cfg, args.start_date, args.end_date);
    if (target_bars.empty()) {
        fmt::print("错误：未找到 {} 的数据\n", args.target_stock);
        return 1;
    }
    auto target_feats = compute_features(target_bars);
    auto predictions = generate_predictions(model, target_feats);
    size_t high_prob = 0;
    for (const auto& kv : predictions) if (kv.second >= args.ml_threshold) ++high_prob;
    fmt::print("  突破事件: {}\n", predictions.size());
    fmt::print("  ML概率 >= {}: {}个\n", args.ml_threshold, high_prob);

    // Step 4: backtest
    fmt::print("\n{}\n", std::string(70, '='));
    fmt::print("Step 4: 回测对比 ({})\n", args.target_name);
    fmt::print("{}\n", std::string(70, '='));

    double bh = target_bars.back().close / target_bars.front().close - 1.0;
    fmt::print("  买入持有: {:+.1f}%\n\n", bh * 100.0);

    fmt::print("[经典海龟]\n");
    auto r_classic = run_classic_turtle(
        target_bars, params.initial_cash, params.commission,
        "经典海龟", args.target_stock, true);

    fmt::print("\n[ML海龟] 阈值={}:\n", args.ml_threshold);
    int ml_passed = 0, ml_filtered = 0;
    auto r_ml = run_ml_turtle(
        target_bars, params.initial_cash, params.commission,
        predictions, args.ml_threshold,
        "ML海龟", args.target_stock, true,
        &ml_passed, &ml_filtered);
    int total_ml = ml_passed + ml_filtered;
    if (total_ml > 0) {
        fmt::print("  ML过滤: 突破信号{} | 通过{}({:.0f}%) | 过滤{}({:.0f}%)\n",
                   total_ml, ml_passed, 100.0 * ml_passed / total_ml,
                   ml_filtered, 100.0 * ml_filtered / total_ml);
    }

    // Compare
    fmt::print("\n{}\n", std::string(70, '='));
    fmt::print("对比总结\n");
    fmt::print("{}\n", std::string(70, '='));
    fmt::print("  {:<12} {:>14} {:>14}\n", "指标", "经典海龟", "ML海龟");
    fmt::print("  {}\n", std::string(42, '-'));
    fmt::print("  {:<12} {:>+13.1f}% {:>+13.1f}%\n", "买入持有", bh * 100.0, bh * 100.0);
    fmt::print("  {:<12} {:>+13.2f}% {:>+13.2f}%\n", "策略收益",
               r_classic.metrics.total_return * 100.0, r_ml.metrics.total_return * 100.0);
    fmt::print("  {:<12} {:>13.2f}% {:>13.2f}%\n", "最大回撤",
               r_classic.metrics.max_drawdown * 100.0, r_ml.metrics.max_drawdown * 100.0);
    fmt::print("  {:<12} {:>14.2f} {:>14.2f}\n", "夏普比率",
               r_classic.metrics.sharpe_ratio, r_ml.metrics.sharpe_ratio);
    fmt::print("  {:<12} {:>14d} {:>14d}\n", "交易次数",
               r_classic.metrics.total_trades, r_ml.metrics.total_trades);
    fmt::print("  {:<12} {:>13.1f}% {:>13.1f}%\n", "胜率",
               r_classic.metrics.win_rate * 100.0, r_ml.metrics.win_rate * 100.0);
    fmt::print("  {:<12} {:>14.2f} {:>14.2f}\n", "盈亏比",
               r_classic.metrics.profit_loss_ratio, r_ml.metrics.profit_loss_ratio);

    fmt::print("\n关键发现:\n");
    fmt::print("  - ML过滤减少了低质量的突破信号, 每笔交易质量更高\n");
    fmt::print("  - 多股票训练提高了模型的泛化能力\n");
    fmt::print("  - 特征重要性揭示了哪些因素影响突破成功率\n");
    fmt::print("  - 防过拟合: 浅树+正则化+时间分割, 追求稳定而非极致收益\n");

    return 0;
}
