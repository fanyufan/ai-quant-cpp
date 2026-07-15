// 经典海龟交易策略 - ATR仓位管理 + 金字塔加仓
// 对应 week4/8-海龟交易法则-20260311/1-经典海龟策略.py

#include <cmath>
#include <iostream>
#include <string>
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
        } else if ((arg == "--start") && i + 1 < argc) {
            a.start_date = argv[++i];
        } else if ((arg == "--end") && i + 1 < argc) {
            a.end_date = argv[++i];
        } else if ((arg == "--data-file") && i + 1 < argc) {
            a.data_file = argv[++i];
        }
    }
    return a;
}

std::vector<bt::Bar> load_bars(const Args& a, const quant::mysql::Config& cfg) {
    std::vector<bt::Bar> bars;
    if (!a.data_file.empty()) {
        bars = bt::data::load_from_csv(a.data_file, a.start_date, a.end_date);
    } else {
        bars = bt::data::load_from_mysql(cfg, a.stock_code, a.start_date, a.end_date);
    }
    return bars;
}

struct BacktestOutput {
    bt::Result result;
    bt::Metrics metrics;
};

BacktestOutput run_simple_turtle(const std::vector<bt::Bar>& bars,
                                 double initial_cash,
                                 double commission,
                                 double position_pct,
                                 int entry_period,
                                 int exit_period,
                                 const std::string& label,
                                 const std::string& stock_code,
                                 bool do_plot) {
    class SimpleTurtle : public bt::Strategy {
    public:
        SimpleTurtle(double position_pct, const std::vector<double>& entry_high,
                     const std::vector<double>& exit_low)
            : position_pct_(position_pct), entry_high_(entry_high), exit_low_(exit_low) {}

        void next(size_t idx, const std::vector<bt::Bar>& bars, bt::Broker& broker) override {
            if (std::isnan(entry_high_[idx]) || std::isnan(exit_low_[idx])) return;
            double close = bars[idx].close;
            if (broker.shares() == 0) {
                if (close > entry_high_[idx]) {
                    double target_cash = broker.cash() * position_pct_ / 100.0;
                    broker.buy(idx, bars, target_cash);
                }
            } else {
                if (close < exit_low_[idx]) {
                    broker.sell(idx, bars);
                }
            }
        }

    private:
        double position_pct_;
        const std::vector<double>& entry_high_;
        const std::vector<double>& exit_low_;
    };

    std::vector<double> highs, lows;
    highs.reserve(bars.size());
    lows.reserve(bars.size());
    for (const auto& b : bars) {
        highs.push_back(b.high);
        lows.push_back(b.low);
    }
    auto entry_high = turtle::rolling_max_prev(highs, entry_period);
    auto exit_low = turtle::rolling_min_prev(lows, exit_period);

    SimpleTurtle strategy(position_pct, entry_high, exit_low);
    bt::Backtest bt(initial_cash, commission);
    BacktestOutput out;
    out.result = bt.run(bars, strategy);
    out.metrics = bt::compute_metrics(out.result);

    fmt::print("[{}] {}\n", label, stock_code);
    bt::print_metrics_line(out.metrics);
    if (do_plot) {
        std::string safe = label;
        for (auto& c : safe) {
            if (c == ' ' || c == '/') c = '_';
        }
        bt::plot_backtest(out.result, bars, stock_code, label, "outputs/" + safe + ".png");
        fmt::print("  图表已保存: outputs/{}.png\n", safe);
    }
    return out;
}

BacktestOutput run_full_turtle(const std::vector<bt::Bar>& bars,
                               double initial_cash,
                               double commission,
                               int entry_period,
                               int exit_period,
                               int atr_period,
                               double risk_pct,
                               int max_units,
                               double add_n,
                               double stop_n,
                               const std::string& label,
                               const std::string& stock_code,
                               bool do_plot) {
    class FullTurtle : public bt::Strategy {
    public:
        FullTurtle(double risk_pct, int max_units, double add_n, double stop_n,
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
        void reset() {
            units_ = 0;
            last_add_price_ = 0.0;
            stop_price_ = 0.0;
        }
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
    highs.reserve(bars.size());
    lows.reserve(bars.size());
    closes.reserve(bars.size());
    for (const auto& b : bars) {
        highs.push_back(b.high);
        lows.push_back(b.low);
        closes.push_back(b.close);
    }

    auto entry_high = turtle::rolling_max_prev(highs, entry_period);
    auto exit_low = turtle::rolling_min_prev(lows, exit_period);
    auto atr = ind::atr(highs, lows, closes, atr_period);

    FullTurtle strategy(risk_pct, max_units, add_n, stop_n, entry_high, exit_low, atr);
    bt::Backtest bt(initial_cash, commission);
    BacktestOutput out;
    out.result = bt.run(bars, strategy);
    out.metrics = bt::compute_metrics(out.result);

    fmt::print("[{}] {}\n", label, stock_code);
    bt::print_metrics_line(out.metrics);
    if (do_plot) {
        std::string safe = label;
        for (auto& c : safe) {
            if (c == ' ' || c == '/') c = '_';
        }
        bt::plot_backtest(out.result, bars, stock_code, label, "outputs/" + safe + ".png");
        fmt::print("  图表已保存: outputs/{}.png\n", safe);
    }
    return out;
}

double calc_buy_and_hold(const std::vector<bt::Bar>& bars) {
    if (bars.size() < 2) return 0.0;
    return bars.back().close / bars.front().close - 1.0;
}

} // namespace

int main(int argc, char* argv[]) {
    auto args = parse_args(argc, argv);
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);
    auto params = bt::load_backtest_params(env);

    fmt::print("{}\n", std::string(70, '='));
    fmt::print("海龟交易法则 - 经典策略实战\n");
    fmt::print("{}\n", std::string(70, '='));
    fmt::print("\n海龟四大组件:\n");
    fmt::print("  1. 唐奇安通道: 突破20日最高价入场, 跌破10日最低价出场\n");
    fmt::print("  2. ATR(N值):   衡量市场波动幅度\n");
    fmt::print("  3. ATR仓位:    单位大小 = (账户资金 * 1%) / ATR\n");
    fmt::print("  4. 金字塔加仓: 最多4个单位, 每上涨0.5N加仓, 2N止损\n");

    // Part 1
    fmt::print("\n{}\n", std::string(70, '='));
    fmt::print("Part 1: 买入持有 vs 简单海龟 vs 完整海龟 ({} {})\n", args.stock_code, args.stock_name);
    fmt::print("{}\n", std::string(70, '='));

    auto demo_bars = load_bars(args, cfg);
    if (demo_bars.empty()) {
        fmt::print("错误：未找到 {} 的数据\n", args.stock_code);
        return 1;
    }

    fmt::print("\n[简单海龟] 仅唐奇安通道信号, 固定仓位{:.0f}%:\n", params.position_pct);
    auto r_simple = run_simple_turtle(
        demo_bars, params.initial_cash, params.commission, params.position_pct,
        20, 10, "简单海龟", args.stock_code, true);

    fmt::print("\n[完整海龟] ATR仓位管理 + 金字塔加仓 + 2N止损:\n");
    auto r_full = run_full_turtle(
        demo_bars, params.initial_cash, params.commission,
        20, 10, 20, 0.01, 4, 0.5, 2.0,
        "完整海龟", args.stock_code, true);

    double bh = calc_buy_and_hold(demo_bars);

    fmt::print("\n{}\n", std::string(60, '='));
    fmt::print("  三策略对比 \n");
    print_three_strategy_table(bh, r_simple.metrics, r_full.metrics);

    // Part 2
    fmt::print("\n{}\n", std::string(70, '='));
    fmt::print("Part 2: 三策略在不同标的上的表现\n");
    fmt::print("{}\n", std::string(70, '='));
    fmt::print("  趋势跟踪策略的核心前提: 市场存在趋势\n");
    fmt::print("  下面对比: 下跌股 vs 上涨ETF, 看三种策略的差异\n");

    std::vector<std::pair<std::string, std::string>> stocks = {
        {"600519.SH", "贵州茅台"},
        {"510300.SH", "沪深300ETF"},
        {"159941.SZ", "纳指ETF"},
    };

    struct StockResult {
        std::string name;
        BacktestOutput simple;
        BacktestOutput full;
        double bh = 0.0;
    };
    std::vector<std::pair<std::string, StockResult>> results;

    for (const auto& [code, name] : stocks) {
        Args a2 = args;
        a2.stock_code = code;
        a2.stock_name = name;
        a2.data_file.clear(); // only the demo may come from CSV
        auto bars = load_bars(a2, cfg);
        if (bars.empty()) {
            fmt::print("\n--- {}({}) ---\n", name, code);
            fmt::print("  跳过: 未找到数据\n");
            continue;
        }
        fmt::print("\n--- {}({}) ---\n", name, code);
        StockResult sr;
        sr.name = name;
        sr.simple = run_simple_turtle(
            bars, params.initial_cash, params.commission, params.position_pct,
            20, 10, name + "-简单海龟", code, true);
        sr.full = run_full_turtle(
            bars, params.initial_cash, params.commission,
            20, 10, 20, 0.01, 4, 0.5, 2.0,
            name + "-完整海龟", code, true);
        sr.bh = calc_buy_and_hold(bars);
        results.emplace_back(code, sr);
    }

    for (const auto& [code, sr] : results) {
        fmt::print("\n{}\n", std::string(70, '='));
        fmt::print("{}({}) 三策略对比\n", sr.name, code);
        fmt::print("{}\n", std::string(70, '='));
        print_three_strategy_table(sr.bh, sr.simple.metrics, sr.full.metrics);
    }

    if (!results.empty()) {
        fmt::print("\n{}\n", std::string(70, '='));
        fmt::print("多标的汇总对比\n");
        fmt::print("{}\n", std::string(70, '='));

        std::vector<std::string> names;
        for (const auto& [code, sr] : results) names.push_back(sr.name);
        const int col_width = 12;
        std::string header = fmt::format("  {:<16}", "指标");
        for (const auto& n : names) header += fmt::format(" {:>{}}", n, col_width);
        int sep_len = 16 + (col_width + 1) * static_cast<int>(names.size());

        std::vector<std::pair<std::string, std::string>> strategy_keys = {
            {"买入持有", "bh"}, {"简单海龟", "simple"}, {"完整海龟", "full"}};
        for (const auto& [label, key] : strategy_keys) {
            fmt::print("\n  [{}]\n", label);
            fmt::print("{}\n", header);
            fmt::print("  {}\n", std::string(sep_len, '-'));
            if (key == "bh") {
                std::string row = fmt::format("  {:<16}", "总收益");
                for (const auto& [code, sr] : results) {
                    row += fmt::format(" {:>+{}.1f}%", col_width - 1, sr.bh * 100.0);
                }
                fmt::print("{}\n", row);
            } else {
                auto fmt_metric = [&](const char* label_cn, auto fn) {
                    std::string row = fmt::format("  {:<16}", label_cn);
                    for (const auto& [code, sr] : results) {
                        const auto& m = (key == "simple") ? sr.simple.metrics : sr.full.metrics;
                        row += " " + fn(m, col_width);
                    }
                    fmt::print("{}\n", row);
                };
                fmt_metric("总收益", [](const bt::Metrics& m, int w) {
                    return fmt::format("{:>+{}.1f}%", w - 1, m.total_return * 100.0);
                });
                fmt_metric("最大回撤", [](const bt::Metrics& m, int w) {
                    return fmt::format("{:>{}.1f}%", w - 1, m.max_drawdown * 100.0);
                });
                fmt_metric("夏普比率", [](const bt::Metrics& m, int w) {
                    return fmt::format("{:>{}.2f}", w, m.sharpe_ratio);
                });
                fmt_metric("交易次数", [](const bt::Metrics& m, int w) {
                    return fmt::format("{:>{}d}", w, m.total_trades);
                });
                fmt_metric("胜率", [](const bt::Metrics& m, int w) {
                    return fmt::format("{:>{}.1f}%", w - 1, m.win_rate * 100.0);
                });
                fmt_metric("盈亏比", [](const bt::Metrics& m, int w) {
                    return fmt::format("{:>{}.2f}", w, m.profit_loss_ratio);
                });
            }
        }
    }

    fmt::print("\n关键发现:\n");
    fmt::print("  - 海龟策略在有明确趋势的市场中表现优异\n");
    fmt::print("  - 在下跌/震荡市场中, 趋势跟踪策略会频繁假突破而亏损\n");
    fmt::print("  - 简单海龟({:.0f}%仓位)收益更高, 但回撤也更大 -- 高仓位是双刃剑\n", params.position_pct);
    fmt::print("  - 完整海龟通过ATR仓位管理控制风险, 回撤更小, 但牺牲了收益弹性\n");
    fmt::print("  - 核心启示: 趋势策略不是万能的, 选择合适的市场比优化参数更重要\n");

    return 0;
}
