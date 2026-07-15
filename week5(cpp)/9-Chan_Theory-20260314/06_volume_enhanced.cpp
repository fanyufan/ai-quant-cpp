// 6-缠论+量价增强策略
// 对应 week5/9-缠论量化-20260314/CASE-缠论精华量化/6-缠论+量价增强策略.py

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <iostream>
#include <vector>

#include "backtest.hpp"
#include "backtest_config.hpp"
#include "backtest_data_mysql.hpp"
#include "backtest_plot.hpp"
#include "backtest_report.hpp"
#include "chan_analyzer.hpp"
#include "env.hpp"
#include "indicators.hpp"

using namespace quant;

class ChanBasicStrategy : public bt::Strategy {
public:
    ChanBasicStrategy(double position_pct)
        : position_pct_(position_pct) {}

    void next(size_t idx, const std::vector<bt::Bar>& bars, bt::Broker& broker) override {
        double close = bars[idx].close;
        if (broker.shares() == 0) {
            if (bars[idx].chan_signal == 3) {
                double target_cash = broker.nav(close) * position_pct_ / 100.0;
                broker.buy(idx, bars, target_cash);
                entry_price_ = close;
                stop_price_ = (bars[idx].chan_zg > 0.0) ? bars[idx].chan_zg : close * 0.93;
            }
        } else {
            bool sell = false;
            if (close < stop_price_) sell = true;
            if (close / entry_price_ - 1.0 >= 0.15) sell = true;
            if (bars[idx].chan_signal == -3) sell = true;
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

class ChanEnhancedStrategy : public bt::Strategy {
public:
    ChanEnhancedStrategy(double position_pct, const std::vector<double>& atr)
        : position_pct_(position_pct), atr_(atr) {}

    void next(size_t idx, const std::vector<bt::Bar>& bars, bt::Broker& broker) override {
        double close = bars[idx].close;
        if (broker.shares() == 0) {
            if (bars[idx].chan_signal == 3) {
                double target_cash = broker.nav(close) * position_pct_ / 100.0;
                broker.buy(idx, bars, target_cash);
                entry_price_ = close;
                highest_price_ = close;
                stop_price_ = (bars[idx].chan_zg > 0.0) ? bars[idx].chan_zg : close * 0.93;
            }
        } else {
            highest_price_ = std::max(highest_price_, close);
            double profit_pct = close / entry_price_ - 1.0;
            if (profit_pct >= 0.10) {
                stop_price_ = std::max(stop_price_, entry_price_ * 1.05);
            } else if (profit_pct >= 0.05) {
                stop_price_ = std::max(stop_price_, entry_price_);
            }
            double atr_stop = highest_price_ - 2.5 * atr_[idx];
            if (!std::isnan(atr_stop) && atr_stop > stop_price_) {
                stop_price_ = atr_stop;
            }

            bool sell = false;
            if (close < stop_price_) sell = true;
            if (bars[idx].chan_signal == -3) sell = true;
            if (sell) {
                broker.sell(idx, bars);
                entry_price_ = 0.0;
                highest_price_ = 0.0;
                stop_price_ = 0.0;
            }
        }
    }

private:
    double position_pct_;
    const std::vector<double>& atr_;
    double entry_price_ = 0.0;
    double highest_price_ = 0.0;
    double stop_price_ = 0.0;
};

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

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);
    auto params = bt::load_backtest_params(env);

    const std::string stock_code = "600519.SH";
    const std::string stock_name = "贵州茅台";
    const std::string start_date = "2023-01-01";
    const std::string end_date = "2025-12-31";

    auto raw_bars = bt::data::load_from_mysql(cfg, stock_code, start_date, end_date);
    if (raw_bars.empty()) {
        fmt::print("未找到 {} 的数据\n", stock_code);
        return 1;
    }
    auto bars = annotate_bars(raw_bars);

    std::vector<double> highs, lows, closes;
    for (const auto& b : bars) {
        highs.push_back(b.high);
        lows.push_back(b.low);
        closes.push_back(b.close);
    }
    auto atr14 = ind::atr(highs, lows, closes, 14);

    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("缠论+量价增强策略 | {}({})\n", stock_name, stock_code);
    fmt::print("{0}\n", std::string(70, '='));

    bt::Backtest bt(params.initial_cash, params.commission);

    ChanBasicStrategy basic(params.position_pct);
    auto r_basic = bt.run(bars, basic);
    auto m_basic = bt::compute_metrics(r_basic);

    ChanEnhancedStrategy enhanced(params.position_pct, atr14);
    auto r_enhanced = bt.run(bars, enhanced);
    auto m_enhanced = bt::compute_metrics(r_enhanced);

    double bh_return = bars.back().close / bars.front().close - 1.0;

    auto print_row = [](const std::string& name, const bt::Metrics& m) {
        fmt::print("  {:<12} {:>+10.2f}% {:>10.2f}% {:>10.2f}% {:>10.2f} {:>8} {:>10.1f}% {:>10.2f}\n",
                   name, m.total_return * 100.0, m.annual_return * 100.0,
                   m.max_drawdown * 100.0, m.sharpe_ratio, m.total_trades,
                   m.win_rate * 100.0, m.profit_loss_ratio);
    };

    fmt::print("\n策略对比:\n");
    fmt::print("  {:<12} {:>11} {:>11} {:>11} {:>10} {:>8} {:>11} {:>10}\n",
               "策略", "总收益", "年化", "最大回撤", "夏普", "交易数", "胜率", "盈亏比");
    fmt::print("  买入持有     {:>+10.2f}%\n", bh_return * 100.0);
    print_row("基础-固定止盈", m_basic);
    print_row("增强-跟踪止损", m_enhanced);

    bt::write_result_csv(r_basic, "outputs/基础-固定止盈");
    bt::write_result_csv(r_enhanced, "outputs/增强-跟踪止损");
    bt::plot_backtest(r_basic, bars, stock_code, "基础-固定止盈", "outputs/基础-固定止盈.png");
    bt::plot_backtest(r_enhanced, bars, stock_code, "增强-跟踪止损", "outputs/增强-跟踪止损.png");

    return 0;
}
