// 多周期融合海龟策略 - 周线定方向, 日线找入场
// 对应 week4/8-海龟交易法则-20260311/3-多周期海龟策略.py

#include <cmath>
#include <iostream>
#include <string>
#include <unordered_map>
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
#include "weekly_bars.hpp"

using namespace quant;

namespace {

struct Args {
    std::string stock_code = "510300.SH";
    std::string stock_name = "沪深300ETF";
    std::string start_date = "2024-01-01";
    std::string end_date = "2025-12-31";
    std::string data_file;
};

Args parse_args(int argc, char* argv[]) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--stock" || arg == "-s") && i + 1 < argc) {
            a.stock_code = argv[++i];
            a.stock_name = a.stock_code;
        } else if (arg == "--start" && i + 1 < argc) a.start_date = argv[++i];
        else if (arg == "--end" && i + 1 < argc) a.end_date = argv[++i];
        else if (arg == "--data-file" && i + 1 < argc) a.data_file = argv[++i];
    }
    return a;
}

std::vector<bt::Bar> load_bars(const Args& a, const quant::mysql::Config& cfg) {
    if (!a.data_file.empty()) {
        return bt::data::load_from_csv(a.data_file, a.start_date, a.end_date);
    }
    return bt::data::load_from_mysql(cfg, a.stock_code, a.start_date, a.end_date);
}

struct Output {
    bt::Result result;
    bt::Metrics metrics;
};

Output run_single_tf(const std::vector<bt::Bar>& bars,
                     double initial_cash,
                     double commission,
                     const std::string& label,
                     const std::string& stock_code,
                     bool do_plot) {
    class SingleTF : public bt::Strategy {
    public:
        SingleTF(double risk_pct, int max_units, double add_n, double stop_n,
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

    SingleTF strategy(0.01, 4, 0.5, 2.0, entry_high, exit_low, atr);
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

// Compute weekly trend for each week using prior completed weeks.
// trend: 1=up, -1=down, 0=neutral.
std::vector<int> compute_weekly_trend(const std::vector<bt::Bar>& weekly, size_t period) {
    std::vector<int> trend(weekly.size(), 0);
    if (weekly.size() <= period + 1) return trend;

    std::vector<double> highs, lows, closes;
    highs.reserve(weekly.size()); lows.reserve(weekly.size()); closes.reserve(weekly.size());
    for (const auto& b : weekly) {
        highs.push_back(b.high);
        lows.push_back(b.low);
        closes.push_back(b.close);
    }
    auto high_prev = turtle::rolling_max_prev(highs, period);
    auto low_prev = turtle::rolling_min_prev(lows, period);

    for (size_t i = period; i < weekly.size(); ++i) {
        if (std::isnan(high_prev[i]) || std::isnan(low_prev[i])) continue;
        if (closes[i] > high_prev[i]) trend[i] = 1;
        else if (closes[i] < low_prev[i]) trend[i] = -1;
        else trend[i] = 0;
    }
    return trend;
}

Output run_multi_tf(const std::vector<bt::Bar>& bars,
                    double initial_cash,
                    double commission,
                    const std::string& label,
                    const std::string& stock_code,
                    bool do_plot) {
    class MultiTF : public bt::Strategy {
    public:
        MultiTF(double risk_pct, int max_units, double add_n, double stop_n,
                const std::vector<double>& entry_high,
                const std::vector<double>& exit_low,
                const std::vector<double>& atr,
                const std::vector<int>& weekly_trend,
                const std::vector<int>& daily_week_idx)
            : risk_pct_(risk_pct), max_units_(max_units), add_n_(add_n), stop_n_(stop_n),
              entry_high_(entry_high), exit_low_(exit_low), atr_(atr),
              weekly_trend_(weekly_trend), daily_week_idx_(daily_week_idx) {}

        void next(size_t idx, const std::vector<bt::Bar>& bars, bt::Broker& broker) override {
            double atr = atr_[idx];
            if (std::isnan(atr) || atr <= 0.0) return;
            if (std::isnan(entry_high_[idx]) || std::isnan(exit_low_[idx])) return;
            double close = bars[idx].close;

            int widx = daily_week_idx_[idx];
            int w_trend = (widx > 0) ? weekly_trend_[widx - 1] : 0; // use prior completed week

            if (broker.shares() == 0) {
                if (w_trend == -1) return; // 大趋势向下禁止做多
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
                if (w_trend == -1) {
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
        const std::vector<int>& weekly_trend_;
        const std::vector<int>& daily_week_idx_;
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

    auto weekly = bt::resample_to_weekly(bars);
    auto weekly_trend = compute_weekly_trend(weekly, 8);

    std::unordered_map<std::string, size_t> week_start_to_idx;
    for (size_t i = 0; i < weekly.size(); ++i) {
        week_start_to_idx[bt::week_start(weekly[i].date)] = i;
    }
    std::vector<int> daily_week_idx(bars.size(), -1);
    for (size_t i = 0; i < bars.size(); ++i) {
        auto it = week_start_to_idx.find(bt::week_start(bars[i].date));
        if (it != week_start_to_idx.end()) daily_week_idx[i] = static_cast<int>(it->second);
    }

    MultiTF strategy(0.01, 4, 0.5, 2.0, entry_high, exit_low, atr, weekly_trend, daily_week_idx);
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

    fmt::print("{}\n", std::string(70, '='));
    fmt::print("多周期融合海龟策略: 周线定方向 + 日线找入场\n");
    fmt::print("{}\n", std::string(70, '='));
    fmt::print("\n多周期思想:\n");
    fmt::print("  周线通道(8周): 判断大趋势方向\n");
    fmt::print("  日线通道(20日): 寻找入场时机\n");
    fmt::print("  规则: 只有周线趋势向下时才禁止做多, 其他情况正常交易\n");
    fmt::print("  效果: 避免在大趋势下跌时逆势做多\n");

    auto bars = load_bars(args, cfg);
    if (bars.empty()) {
        fmt::print("错误：未找到 {} 的数据\n", args.stock_code);
        return 1;
    }
    double bh = bars.back().close / bars.front().close - 1.0;

    fmt::print("\n{}\n", std::string(70, '-'));
    fmt::print("[单周期] 仅日线海龟 | 买入持有: {:+.1f}%\n", bh * 100.0);
    fmt::print("{}\n", std::string(70, '-'));
    auto r_single = run_single_tf(bars, params.initial_cash, params.commission,
                                  "单周期海龟", args.stock_code, true);

    fmt::print("\n{}\n", std::string(70, '-'));
    fmt::print("[多周期] 周线过滤(只挡下跌) + 日线入场\n");
    fmt::print("{}\n", std::string(70, '-'));
    auto r_multi = run_multi_tf(bars, params.initial_cash, params.commission,
                                "多周期海龟", args.stock_code, true);

    fmt::print("\n{}\n", std::string(70, '='));
    fmt::print("对比总结\n");
    fmt::print("{}\n", std::string(70, '='));
    fmt::print("  {:<12} {:>14} {:>14}\n", "指标", "单周期", "多周期");
    fmt::print("  {}\n", std::string(42, '-'));
    fmt::print("  {:<12} {:>+13.1f}% {:>+13.1f}%\n", "买入持有", bh * 100.0, bh * 100.0);
    fmt::print("  {:<12} {:>+13.2f}% {:>+13.2f}%\n", "海龟收益",
               r_single.metrics.total_return * 100.0, r_multi.metrics.total_return * 100.0);
    fmt::print("  {:<12} {:>13.2f}% {:>13.2f}%\n", "最大回撤",
               r_single.metrics.max_drawdown * 100.0, r_multi.metrics.max_drawdown * 100.0);
    fmt::print("  {:<12} {:>14.2f} {:>14.2f}\n", "夏普比率",
               r_single.metrics.sharpe_ratio, r_multi.metrics.sharpe_ratio);
    fmt::print("  {:<12} {:>14d} {:>14d}\n", "交易次数",
               r_single.metrics.total_trades, r_multi.metrics.total_trades);
    fmt::print("  {:<12} {:>13.1f}% {:>13.1f}%\n", "胜率",
               r_single.metrics.win_rate * 100.0, r_multi.metrics.win_rate * 100.0);
    fmt::print("  {:<12} {:>14.2f} {:>14.2f}\n", "盈亏比",
               r_single.metrics.profit_loss_ratio, r_multi.metrics.profit_loss_ratio);

    fmt::print("\n关键发现:\n");
    fmt::print("  - 周线过滤只挡住'大趋势明确向下'的情况, 不过度限制\n");
    fmt::print("  - 减少了逆势交易, 但保留了趋势启动时的入场机会\n");
    fmt::print("  - 多周期适合中长线交易, ETF/指数类标的效果较好\n");

    return 0;
}
