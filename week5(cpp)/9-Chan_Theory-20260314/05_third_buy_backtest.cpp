// 5-缠论三买策略回测
// 对应 week5/9-缠论量化-20260314/CASE-缠论精华量化/5-缠论三买策略回测.py

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

using namespace quant;

class ChanThirdBuyStrategy : public bt::Strategy {
public:
    ChanThirdBuyStrategy(double position_pct)
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

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);
    auto params = bt::load_backtest_params(env);

    const std::string stock_code = "600519.SH";
    const std::string stock_name = "贵州茅台";
    const std::string start_date = "2023-01-01";
    const std::string end_date = "2025-12-31";

    auto bars = bt::data::load_from_mysql(cfg, stock_code, start_date, end_date);
    if (bars.empty()) {
        fmt::print("未找到 {} 的数据\n", stock_code);
        return 1;
    }

    // Annotate bars with Chan signals.
    chan::ChanAnalyzer analyzer(bars);
    analyzer.analyze();
    auto sig_map = analyzer.get_signal_map();
    auto zg_map = analyzer.get_zg_map();
    auto zd_map = analyzer.get_zd_map();
    for (auto& b : bars) {
        auto it = sig_map.find(b.date);
        b.chan_signal = (it != sig_map.end()) ? it->second : 0;
        b.chan_zg = zg_map[b.date];
        b.chan_zd = zd_map[b.date];
    }

    int third_buy_count = 0, third_sell_count = 0;
    for (const auto& s : analyzer.signals()) {
        if (s.type == chan::SignalType::ThirdBuy) ++third_buy_count;
        if (s.type == chan::SignalType::ThirdSell) ++third_sell_count;
    }

    fmt::print("\n{0}\n", std::string(60, '='));
    fmt::print("缠论三买策略回测 | {}({})\n", stock_name, stock_code);
    fmt::print("{0}\n", std::string(60, '='));
    fmt::print("  三买信号数: {} | 三卖信号数: {}\n", third_buy_count, third_sell_count);

    bt::Backtest bt(params.initial_cash, params.commission);
    ChanThirdBuyStrategy strategy(params.position_pct);
    auto result = bt.run(bars, strategy);
    auto metrics = bt::compute_metrics(result);

    bt::print_summary(result, stock_code, stock_name, start_date, end_date);
    bt::print_metrics_line(metrics);

    double bh_return = bars.back().close / bars.front().close - 1.0;
    fmt::print("  买入持有收益: {:+.2f}% | 超额收益: {:+.2f}%\n",
               bh_return * 100.0, (metrics.total_return - bh_return) * 100.0);

    bt::write_result_csv(result, "outputs/缠论三买策略");
    bt::plot_backtest(result, bars, stock_code, "缠论三买策略", "outputs/缠论三买策略.png");
    return 0;
}
