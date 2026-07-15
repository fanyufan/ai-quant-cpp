// 8-ML增强缠论策略
// 对应 week5/9-缠论量化-20260314/CASE-缠论精华量化/8-ML增强缠论策略.py

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <iostream>
#include <map>
#include <numeric>
#include <vector>

#include "backtest.hpp"
#include "backtest_config.hpp"
#include "backtest_data_mysql.hpp"
#include "backtest_plot.hpp"
#include "backtest_report.hpp"
#include "chan_analyzer.hpp"
#include "env.hpp"
#include "indicators.hpp"
#include "ml_tree.hpp"

using namespace quant;

struct Sample {
    std::string date;
    std::vector<double> x;
    int y = 0;
};

namespace {

std::vector<bt::Bar> annotate_bars(const std::vector<bt::Bar>& bars) {
    std::vector<bt::Bar> out = bars;
    chan::ChanAnalyzer analyzer(out);
    analyzer.analyze();
    auto sig_map = analyzer.get_signal_map();
    auto zg_map = analyzer.get_zg_map();
    auto zd_map = analyzer.get_zd_map();
    for (auto& b : out) {
        auto it = sig_map.find(b.date);
        b.chan_signal = (it != sig_map.end()) ? it->second : 0;
        b.chan_zg = zg_map[b.date];
        b.chan_zd = zd_map[b.date];
    }
    return out;
}

size_t find_index(const std::vector<bt::Bar>& bars, const std::string& date) {
    for (size_t i = 0; i < bars.size(); ++i) {
        if (bars[i].date >= date) return i;
    }
    return bars.size();
}

std::vector<Sample> extract_samples(const std::vector<bt::Bar>& bars,
                                    const chan::ChanAnalyzer& analyzer) {
    std::vector<Sample> out;
    if (bars.size() < 60) return out;

    std::vector<double> highs, lows, closes, volumes;
    for (const auto& b : bars) {
        highs.push_back(b.high);
        lows.push_back(b.low);
        closes.push_back(b.close);
        volumes.push_back(b.volume);
    }
    auto atr14 = ind::atr(highs, lows, closes, 14);
    auto adx14 = ind::adx(highs, lows, closes, 14);
    auto rsi14 = ind::rsi(closes, 14);
    auto vol_ma20 = ind::sma(volumes, 20);
    auto macd = ind::macd(closes, 12, 26, 9);

    const auto& signals = analyzer.signals();
    const auto& zs_list = analyzer.zhongshu_list();
    const auto& bi_list = analyzer.bi_list();

    for (const auto& s : signals) {
        if (s.type != chan::SignalType::ThirdBuy) continue;
        size_t idx = find_index(bars, s.date);
        if (idx >= bars.size() || idx + 20 >= bars.size()) continue;
        if (std::isnan(atr14[idx]) || std::isnan(adx14.adx[idx]) ||
            std::isnan(rsi14[idx]) || std::isnan(vol_ma20[idx]) ||
            std::isnan(macd.bar[idx]))
            continue;

        double future_max = bars[idx].close;
        for (size_t k = idx + 1; k <= idx + 20; ++k) {
            future_max = std::max(future_max, bars[k].high);
        }
        int label = (future_max / bars[idx].close - 1.0) > 0.05 ? 1 : 0;

        // Find latest中枢 before signal.
        const chan::Zhongshu* zs = nullptr;
        for (auto it = zs_list.rbegin(); it != zs_list.rend(); ++it) {
            if (it->end_date <= bars[idx].date) { zs = &(*it); break; }
        }
        // Find last up bi before signal.
        const chan::Bi* last_up_bi = nullptr;
        for (auto it = bi_list.rbegin(); it != bi_list.rend(); ++it) {
            if (it->up && it->end_date <= bars[idx].date) { last_up_bi = &(*it); break; }
        }

        double zs_height_ratio = 0.0;
        double zs_width = 0.0;
        double bi_in_zs = 0.0;
        if (zs) {
            zs_height_ratio = (zs->zg - zs->zd) / bars[idx].close;
            zs_width = static_cast<double>(find_index(bars, zs->end_date) - find_index(bars, zs->start_date));
            bi_in_zs = static_cast<double>(zs->bi_count);
        }
        double bi_slope = 0.0;
        if (last_up_bi && last_up_bi->end_idx > last_up_bi->start_idx) {
            int days = static_cast<int>(last_up_bi->end_idx - last_up_bi->start_idx);
            bi_slope = (last_up_bi->end_price / last_up_bi->start_price - 1.0) / days;
        }

        Sample sample;
        sample.date = bars[idx].date;
        sample.x = {
            zs_height_ratio,
            zs_width,
            bi_in_zs,
            bi_slope,
            atr14[idx] / bars[idx].close,
            adx14.adx[idx],
            volumes[idx] / vol_ma20[idx],
            rsi14[idx],
            macd.bar[idx] / bars[idx].close * 100.0,
            closes[idx] / closes[std::max<size_t>(0, idx - 10)] - 1.0,
        };
        sample.y = label;
        out.push_back(std::move(sample));
    }
    return out;
}

} // namespace

class BasicThirdBuyStrategy : public bt::Strategy {
public:
    BasicThirdBuyStrategy(double position_pct) : position_pct_(position_pct) {}

    void next(size_t idx, const std::vector<bt::Bar>& bars, bt::Broker& broker) override {
        double close = bars[idx].close;
        if (broker.shares() == 0) {
            if (bars[idx].chan_signal == 3) {
                broker.buy(idx, bars, broker.nav(close) * position_pct_ / 100.0);
                entry_price_ = close;
                stop_price_ = (bars[idx].chan_zg > 0.0) ? bars[idx].chan_zg : close * 0.93;
            }
        } else {
            bool sell = close < stop_price_ ||
                        (close / entry_price_ - 1.0 >= 0.15) ||
                        (bars[idx].chan_signal == -3);
            if (sell) {
                broker.sell(idx, bars);
                entry_price_ = 0.0;
                stop_price_ = 0.0;
            }
        }
    }

private:
    double position_pct_;
    double entry_price_ = 0.0;
    double stop_price_ = 0.0;
};

class MLThirdBuyStrategy : public bt::Strategy {
public:
    MLThirdBuyStrategy(double position_pct,
                       const std::map<std::string, double>& preds,
                       double threshold,
                       int* passed,
                       int* filtered)
        : position_pct_(position_pct), preds_(preds), threshold_(threshold),
          passed_(passed), filtered_(filtered) {}

    void next(size_t idx, const std::vector<bt::Bar>& bars, bt::Broker& broker) override {
        double close = bars[idx].close;
        if (broker.shares() == 0) {
            if (bars[idx].chan_signal == 3) {
                auto it = preds_.find(bars[idx].date);
                double prob = (it != preds_.end()) ? it->second : 0.0;
                if (prob >= threshold_) {
                    broker.buy(idx, bars, broker.nav(close) * position_pct_ / 100.0);
                    entry_price_ = close;
                    stop_price_ = (bars[idx].chan_zg > 0.0) ? bars[idx].chan_zg : close * 0.93;
                    if (passed_) ++(*passed_);
                } else {
                    if (filtered_) ++(*filtered_);
                }
            }
        } else {
            bool sell = close < stop_price_ ||
                        (close / entry_price_ - 1.0 >= 0.15) ||
                        (bars[idx].chan_signal == -3);
            if (sell) {
                broker.sell(idx, bars);
                entry_price_ = 0.0;
                stop_price_ = 0.0;
            }
        }
    }

private:
    double position_pct_;
    const std::map<std::string, double>& preds_;
    double threshold_;
    int* passed_;
    int* filtered_;
    double entry_price_ = 0.0;
    double stop_price_ = 0.0;
};

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);
    auto params = bt::load_backtest_params(env);

    const std::string start_date = "2022-01-01";
    const std::string end_date = "2025-12-31";
    const std::string split_date = "2025-01-01";
    const std::string target_stock = "688981.SH";
    const std::string target_name = "中芯国际";

    std::vector<std::pair<std::string, std::string>> train_stocks = {
        {"600519.SH", "贵州茅台"},
        {"300750.SZ", "宁德时代"},
        {"000001.SZ", "平安银行"},
        {"688981.SH", "中芯国际"},
        {"601318.SH", "中国平安"},
        {"159941.SZ", "纳指ETF"},
    };

    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("ML增强缠论策略\n");
    fmt::print("{0}\n", std::string(70, '='));

    std::vector<Sample> all_samples;
    for (const auto& [code, name] : train_stocks) {
        auto raw = bt::data::load_from_mysql(cfg, code, start_date, end_date);
        if (raw.empty()) {
            fmt::print("  {}({}): 无数据\n", name, code);
            continue;
        }
        auto bars = annotate_bars(raw);
        chan::ChanAnalyzer analyzer(bars);
        analyzer.analyze();
        auto samples = extract_samples(bars, analyzer);
        if (!samples.empty()) {
            size_t true_count = std::count_if(samples.begin(), samples.end(),
                                              [](const Sample& s) { return s.y == 1; });
            fmt::print("  {}({}): {}个三买样本, 成功率 {:.0f}%\n",
                       name, code, samples.size(), 100.0 * true_count / samples.size());
            all_samples.insert(all_samples.end(), samples.begin(), samples.end());
        } else {
            fmt::print("  {}({}): 无有效样本\n", name, code);
        }
    }

    if (all_samples.size() < 10) {
        fmt::print("\n样本不足({}), 无法训练\n", all_samples.size());
        return 1;
    }

    size_t total_true = std::count_if(all_samples.begin(), all_samples.end(),
                                      [](const Sample& s) { return s.y == 1; });
    fmt::print("\n  合计: {}个三买样本\n", all_samples.size());
    fmt::print("  成功: {} ({:.0f}%)\n", total_true, 100.0 * total_true / all_samples.size());

    std::sort(all_samples.begin(), all_samples.end(),
              [](const Sample& a, const Sample& b) { return a.date < b.date; });

    std::vector<Sample> train, test;
    for (const auto& s : all_samples) {
        if (s.date < split_date) train.push_back(s);
        else test.push_back(s);
    }
    if (train.size() < 5 || test.size() < 3) {
        fmt::print("  样本不足: 训练{} / 测试{}\n", train.size(), test.size());
        return 1;
    }

    std::vector<std::vector<double>> X_train, X_test;
    std::vector<double> y_train, y_test;
    for (const auto& s : train) { X_train.push_back(s.x); y_train.push_back(static_cast<double>(s.y)); }
    for (const auto& s : test) { X_test.push_back(s.x); y_test.push_back(static_cast<double>(s.y)); }

    ml::DecisionTree model(false, 3, 3);
    model.fit(X_train, y_train);

    auto evaluate = [&](const std::vector<std::vector<double>>& X, const std::vector<double>& y) {
        int tp = 0, fp = 0, tn = 0, fn = 0;
        for (size_t i = 0; i < X.size(); ++i) {
            double p = model.predict(X[i]);
            int pred = (p >= 0.5) ? 1 : 0;
            int actual = (y[i] >= 0.5) ? 1 : 0;
            if (pred == 1 && actual == 1) ++tp;
            else if (pred == 1 && actual == 0) ++fp;
            else if (pred == 0 && actual == 0) ++tn;
            else ++fn;
        }
        int total = tp + fp + tn + fn;
        double accuracy = total > 0 ? static_cast<double>(tp + tn) / total : 0.0;
        double precision = (tp + fp) > 0 ? static_cast<double>(tp) / (tp + fp) : 0.0;
        double recall = (tp + fn) > 0 ? static_cast<double>(tp) / (tp + fn) : 0.0;
        double f1 = (precision + recall) > 0 ? 2 * precision * recall / (precision + recall) : 0.0;
        fmt::print("    准确率: {:.1f}%  精确率: {:.1f}%  召回率: {:.1f}%  F1: {:.1f}%\n",
                   accuracy * 100.0, precision * 100.0, recall * 100.0, f1 * 100.0);
    };

    fmt::print("\n  引擎: C++ DecisionTree (max_depth=3)\n");
    fmt::print("  训练集: {} | 测试集: {}\n", train.size(), test.size());
    fmt::print("\n  测试集评估:\n");
    evaluate(X_test, y_test);

    std::vector<std::string> feature_names = {
        "zs_height_ratio", "zs_width", "bi_in_zs", "bi_slope",
        "atr_ratio", "adx", "vol_ratio", "rsi", "macd_hist", "momentum_10d"};
    auto imp = model.feature_importances();
    fmt::print("\n  特征重要性:\n");
    for (size_t i = 0; i < feature_names.size() && i < imp.size(); ++i) {
        std::string bar(static_cast<size_t>(imp[i] * 25), '#');
        fmt::print("    {:<18} {:.2f} {}\n", feature_names[i], imp[i], bar);
    }

    // Predictions for target stock.
    auto target_raw = bt::data::load_from_mysql(cfg, target_stock, start_date, end_date);
    if (target_raw.empty()) {
        fmt::print("\n未找到 {} 的数据\n", target_stock);
        return 1;
    }
    auto target_bars = annotate_bars(target_raw);
    chan::ChanAnalyzer target_analyzer(target_bars);
    target_analyzer.analyze();
    auto target_samples = extract_samples(target_bars, target_analyzer);

    std::map<std::string, double> predictions;
    for (const auto& s : target_samples) {
        predictions[s.date] = model.predict(s.x);
    }
    size_t high_prob = std::count_if(predictions.begin(), predictions.end(),
                                     [](const auto& kv) { return kv.second >= 0.5; });
    fmt::print("\n  目标 {}({}) 三买事件: {} | ML概率>=0.5: {}\n",
               target_name, target_stock, predictions.size(), high_prob);

    // Backtest comparison.
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("回测对比 ({}({}))\n", target_name, target_stock);
    fmt::print("{0}\n", std::string(70, '='));

    double bh_return = target_bars.back().close / target_bars.front().close - 1.0;
    fmt::print("  买入持有: {:+.1f}%\n\n", bh_return * 100.0);

    bt::Backtest bt(params.initial_cash, params.commission);

    BasicThirdBuyStrategy basic(params.position_pct);
    auto r_basic = bt.run(target_bars, basic);
    auto m_basic = bt::compute_metrics(r_basic);
    fmt::print("[基础三买]\n");
    bt::print_metrics_line(m_basic);
    bt::plot_backtest(r_basic, target_bars, target_stock, "基础三买", "outputs/基础三买.png");

    int passed = 0, filtered = 0;
    MLThirdBuyStrategy ml_strat(params.position_pct, predictions, 0.5, &passed, &filtered);
    auto r_ml = bt.run(target_bars, ml_strat);
    auto m_ml = bt::compute_metrics(r_ml);
    fmt::print("\n[ML三买]\n");
    bt::print_metrics_line(m_ml);
    int total_ml = passed + filtered;
    if (total_ml > 0) {
        fmt::print("  ML过滤: 三买{} | 通过{}({:.0f}%) | 过滤{}({:.0f}%)\n",
                   total_ml, passed, 100.0 * passed / total_ml,
                   filtered, 100.0 * filtered / total_ml);
    }
    bt::plot_backtest(r_ml, target_bars, target_stock, "ML三买", "outputs/ML三买.png");

    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("对比总结\n");
    fmt::print("{0}\n", std::string(70, '='));
    fmt::print("  {:<12} {:>14} {:>14}\n", "指标", "基础三买", "ML三买");
    fmt::print("  {}\n", std::string(42, '-'));
    fmt::print("  {:<12} {:>+13.1f}% {:>+13.1f}%\n", "买入持有", bh_return * 100.0, bh_return * 100.0);
    fmt::print("  {:<12} {:>+13.2f}% {:>+13.2f}%\n", "策略收益",
               m_basic.total_return * 100.0, m_ml.total_return * 100.0);
    fmt::print("  {:<12} {:>13.2f}% {:>13.2f}%\n", "最大回撤",
               m_basic.max_drawdown * 100.0, m_ml.max_drawdown * 100.0);
    fmt::print("  {:<12} {:>14.2f} {:>14.2f}\n", "夏普比率", m_basic.sharpe_ratio, m_ml.sharpe_ratio);
    fmt::print("  {:<12} {:>14d} {:>14d}\n", "交易次数", m_basic.total_trades, m_ml.total_trades);
    fmt::print("  {:<12} {:>13.1f}% {:>13.1f}%\n", "胜率",
               m_basic.win_rate * 100.0, m_ml.win_rate * 100.0);

    return 0;
}
