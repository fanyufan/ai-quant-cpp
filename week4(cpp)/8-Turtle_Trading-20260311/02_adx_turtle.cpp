// ADX趋势过滤海龟策略
// 对应 week4/8-海龟交易法则-20260311/2-ADX海龟策略.py

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
    std::string start_date = "2024-01-01";
    std::string end_date = "2025-12-31";
    std::string data_file;
};

Args parse_args(int argc, char* argv[]) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--start") && i + 1 < argc) a.start_date = argv[++i];
        else if ((arg == "--end") && i + 1 < argc) a.end_date = argv[++i];
        else if ((arg == "--data-file") && i + 1 < argc) a.data_file = argv[++i];
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

struct Output {
    bt::Result result;
    bt::Metrics metrics;
};

Output run_turtle(const std::vector<bt::Bar>& bars,
                  double initial_cash,
                  double commission,
                  int entry_period,
                  int exit_period,
                  int atr_period,
                  double risk_pct,
                  int max_units,
                  double add_n,
                  double stop_n,
                  bool use_adx,
                  int adx_period,
                  double adx_threshold,
                  const std::string& label,
                  const std::string& stock_code,
                  bool do_plot) {
    class Turtle : public bt::Strategy {
    public:
        Turtle(double risk_pct, int max_units, double add_n, double stop_n,
               const std::vector<double>& entry_high,
               const std::vector<double>& exit_low,
               const std::vector<double>& atr,
               bool use_adx,
               const std::vector<double>& adx,
               double adx_threshold)
            : risk_pct_(risk_pct), max_units_(max_units), add_n_(add_n), stop_n_(stop_n),
              entry_high_(entry_high), exit_low_(exit_low), atr_(atr),
              use_adx_(use_adx), adx_(adx), adx_threshold_(adx_threshold) {}

        void next(size_t idx, const std::vector<bt::Bar>& bars, bt::Broker& broker) override {
            double atr = atr_[idx];
            if (std::isnan(atr) || atr <= 0.0) return;
            if (std::isnan(entry_high_[idx]) || std::isnan(exit_low_[idx])) return;
            double close = bars[idx].close;

            if (broker.shares() == 0) {
                if (use_adx_) {
                    double adx = adx_[idx];
                    if (std::isnan(adx) || adx < adx_threshold_) return;
                }
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
        bool use_adx_;
        const std::vector<double>& adx_;
        double adx_threshold_;
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
    auto adx_res = ind::adx(highs, lows, closes, adx_period);

    Turtle strategy(risk_pct, max_units, add_n, stop_n,
                    entry_high, exit_low, atr,
                    use_adx, adx_res.adx, adx_threshold);
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
    fmt::print("ADX趋势过滤海龟策略\n");
    fmt::print("{}\n", std::string(70, '='));
    fmt::print("\n原理:\n");
    fmt::print("  ADX(平均趋向指数) 衡量趋势强度, 不分方向\n");
    fmt::print("  ADX > 15: 有一定趋势 -> 允许入场\n");
    fmt::print("  ADX < 15: 趋势极弱 -> 拒绝入场, 避免假突破\n");
    fmt::print("  已持仓时: 出场/加仓逻辑不变, ADX只影响开仓决策\n");

    std::vector<std::pair<std::string, std::string>> stocks = {
        {"601318.SH", "平安银行"},
        {"510300.SH", "沪深300ETF"},
        {"600519.SH", "贵州茅台"},
    };

    struct StockResult {
        std::string name;
        double bh = 0.0;
        Output classic;
        Output adx;
    };
    std::vector<std::pair<std::string, StockResult>> all_results;

    for (const auto& [code, name] : stocks) {
        auto bars = load_bars(code, args.data_file, cfg, args.start_date, args.end_date);
        if (bars.empty()) {
            fmt::print("\n{}\n", std::string(70, '='));
            fmt::print("{} ({})\n", name, code);
            fmt::print("{}\n", std::string(70, '='));
            fmt::print("  跳过: 未找到数据\n");
            continue;
        }

        double bh = bars.back().close / bars.front().close - 1.0;

        fmt::print("\n{}\n", std::string(70, '='));
        fmt::print("{} ({})\n", name, code);
        fmt::print("{}\n", std::string(70, '='));
        fmt::print("  买入持有收益: {:+.1f}%\n\n", bh * 100.0);

        fmt::print("  [经典海龟]\n");
        auto r_classic = run_turtle(
            bars, params.initial_cash, params.commission,
            20, 10, 20, 0.01, 4, 0.5, 2.0,
            false, 14, 15.0,
            "经典海龟", code, true);

        fmt::print("\n  [ADX过滤海龟] ADX > 15 才入场:\n");
        auto r_adx = run_turtle(
            bars, params.initial_cash, params.commission,
            20, 10, 20, 0.01, 4, 0.5, 2.0,
            true, 14, 15.0,
            "ADX海龟", code, true);

        StockResult sr;
        sr.name = name;
        sr.bh = bh;
        sr.classic = r_classic;
        sr.adx = r_adx;
        all_results.emplace_back(code, sr);
    }

    if (!all_results.empty()) {
        fmt::print("\n{}\n", std::string(70, '='));
        fmt::print("汇总: ADX过滤的效果\n");
        fmt::print("{}\n", std::string(70, '='));

        for (const auto& [code, sr] : all_results) {
            const auto& rc = sr.classic.metrics;
            const auto& ra = sr.adx.metrics;
            double dr = (ra.total_return - rc.total_return) * 100.0;
            double dd_diff = (ra.max_drawdown - rc.max_drawdown) * 100.0;
            int trade_diff = static_cast<int>(ra.total_trades) - static_cast<int>(rc.total_trades);

            fmt::print("\n  {} ({}) | 买入持有: {:+.1f}%\n", sr.name, code, sr.bh * 100.0);
            fmt::print("    {:8} {:>12} {:>12} {:>10}\n", "", "经典海龟", "ADX海龟", "变化");
            fmt::print("    {:8} {:>+11.2f}% {:>+11.2f}% {:>+9.2f}%\n",
                       "收益", rc.total_return * 100.0, ra.total_return * 100.0, dr);
            fmt::print("    {:8} {:>11.2f}% {:>11.2f}% {:>+9.2f}%\n",
                       "回撤", rc.max_drawdown * 100.0, ra.max_drawdown * 100.0, dd_diff);
            fmt::print("    {:8} {:>12d} {:>12d} {:>+10d}\n",
                       "交易", rc.total_trades, ra.total_trades, trade_diff);
            fmt::print("    {:8} {:>11.1f}% {:>11.1f}%\n",
                       "胜率", rc.win_rate * 100.0, ra.win_rate * 100.0);
            fmt::print("    {:8} {:>12.2f} {:>12.2f}\n",
                       "盈亏比", rc.profit_loss_ratio, ra.profit_loss_ratio);
        }
    }

    fmt::print("\n关键发现:\n");
    fmt::print("  - ADX过滤以极低的成本(ADX>15是很宽松的条件)过滤掉最差的假突破\n");
    fmt::print("  - 在震荡市中: 减少无效交易, 降低亏损\n");
    fmt::print("  - 在趋势市中: 基本不影响好的信号, 保持收益\n");
    fmt::print("  - ADX阈值不宜太高(如25会误伤好信号), 15是较好的平衡点\n");
    fmt::print("  - 这就是'简单规则往往最有效'的体现 -- 不要过度优化\n");

    return 0;
}
