// 7-多周期缠论策略
// 对应 week5/9-缠论量化-20260314/CASE-缠论精华量化/7-多周期缠论策略.py

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <iostream>
#include <map>
#include <vector>

#include "backtest.hpp"
#include "backtest_config.hpp"
#include "backtest_data_mysql.hpp"
#include "backtest_plot.hpp"
#include "backtest_report.hpp"
#include "chan_analyzer.hpp"
#include "env.hpp"
#include "indicators.hpp"
#include "weekly_bars.hpp"

using namespace quant;

std::map<std::string, int> compute_weekly_trend(const std::vector<bt::Bar>& weekly_bars) {
    std::map<std::string, int> trend_map;  // date -> trend from this date onward
    if (weekly_bars.empty()) return trend_map;

    chan::ChanAnalyzer wa(weekly_bars);
    wa.analyze();
    const auto& zs_list = wa.zhongshu_list();

    std::vector<std::pair<std::string, int>> segments;
    if (zs_list.size() >= 2) {
        for (size_t i = 1; i < zs_list.size(); ++i) {
            const auto& prev = zs_list[i - 1];
            const auto& cur = zs_list[i];
            int trend = 0;
            if (cur.zg > prev.zg && cur.zd > prev.zd) trend = 1;
            else if (cur.zg < prev.zg && cur.zd < prev.zd) trend = -1;
            segments.emplace_back(cur.start_date, trend);
        }
    } else if (!zs_list.empty()) {
        const auto& zs = zs_list.back();
        const auto& last_bar = weekly_bars.back();
        int trend = 0;
        if (last_bar.close > zs.zg) trend = 1;
        else if (last_bar.close < zs.zd) trend = -1;
        segments.emplace_back(zs.start_date, trend);
    }

    // Fallback: weekly MA20.
    std::vector<double> closes;
    for (const auto& b : weekly_bars) closes.push_back(b.close);
    auto ma20 = ind::sma(closes, 20);
    if (!weekly_bars.empty() && !ma20.empty() && !std::isnan(ma20.back())) {
        segments.emplace_back(weekly_bars.back().date,
                              weekly_bars.back().close > ma20.back() ? 1 : -1);
    }

    // Sort segments and build map: each segment date -> trend for dates >= date.
    std::sort(segments.begin(), segments.end());
    for (const auto& seg : segments) {
        trend_map[seg.first] = seg.second;
    }
    return trend_map;
}

std::vector<bt::Bar> annotate_daily_bars(const std::vector<bt::Bar>& daily) {
    std::vector<bt::Bar> out = daily;
    chan::ChanAnalyzer analyzer(out);
    analyzer.analyze();
    auto sig_map = analyzer.get_signal_map();
    auto zg_map = analyzer.get_zg_map();
    auto zd_map = analyzer.get_zd_map();

    auto weekly_bars = bt::resample_to_weekly(out);
    auto trend_segments = compute_weekly_trend(weekly_bars);

    int current_trend = 0;
    for (auto& b : out) {
        auto it = trend_segments.find(b.date);
        if (it != trend_segments.end()) current_trend = it->second;
        b.weekly_trend = current_trend;

        auto sit = sig_map.find(b.date);
        b.chan_signal = (sit != sig_map.end()) ? sit->second : 0;
        b.chan_zg = zg_map[b.date];
        b.chan_zd = zd_map[b.date];
    }
    return out;
}

class SinglePeriodStrategy : public bt::Strategy {
public:
    SinglePeriodStrategy(double position_pct) : position_pct_(position_pct) {}

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

class MultiPeriodStrategy : public bt::Strategy {
public:
    MultiPeriodStrategy(double position_pct) : position_pct_(position_pct) {}

    void next(size_t idx, const std::vector<bt::Bar>& bars, bt::Broker& broker) override {
        double close = bars[idx].close;
        if (broker.shares() == 0) {
            if (bars[idx].weekly_trend == 1 && bars[idx].chan_signal == 3) {
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
            if (bars[idx].weekly_trend == -1) sell = true;
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

    auto raw_bars = bt::data::load_from_mysql(cfg, stock_code, start_date, end_date);
    if (raw_bars.empty()) {
        fmt::print("未找到 {} 的数据\n", stock_code);
        return 1;
    }
    auto bars = annotate_daily_bars(raw_bars);

    int total_third_buy = 0, filtered_third_buy = 0;
    int up_weeks = 0, down_weeks = 0, neutral_weeks = 0;
    for (const auto& b : bars) {
        if (b.chan_signal == 3) ++total_third_buy;
        if (b.weekly_trend == 1 && b.chan_signal == 3) ++filtered_third_buy;
        if (b.weekly_trend == 1) ++up_weeks;
        else if (b.weekly_trend == -1) ++down_weeks;
        else ++neutral_weeks;
    }

    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("多周期缠论策略 | {}({})\n", stock_name, stock_code);
    fmt::print("{0}\n", std::string(70, '='));
    fmt::print("  日线三买总数: {}\n", total_third_buy);
    fmt::print("  周线趋势分布: 上升={} 下跌={} 震荡={}\n", up_weeks, down_weeks, neutral_weeks);
    fmt::print("  周线过滤后三买数: {}\n", filtered_third_buy);

    bt::Backtest bt(params.initial_cash, params.commission);

    SinglePeriodStrategy single(params.position_pct);
    auto r_single = bt.run(bars, single);
    auto m_single = bt::compute_metrics(r_single);

    MultiPeriodStrategy multi(params.position_pct);
    auto r_multi = bt.run(bars, multi);
    auto m_multi = bt::compute_metrics(r_multi);

    double bh_return = bars.back().close / bars.front().close - 1.0;

    auto print_row = [](const std::string& name, const bt::Metrics& m) {
        fmt::print("  {:<14} {:>+10.2f}% {:>10.2f}% {:>10.2f}% {:>10.2f} {:>8} {:>10.1f}%\n",
                   name, m.total_return * 100.0, m.annual_return * 100.0,
                   m.max_drawdown * 100.0, m.sharpe_ratio, m.total_trades,
                   m.win_rate * 100.0);
    };

    fmt::print("\n绩效对比:\n");
    fmt::print("  {:<14} {:>11} {:>10} {:>11} {:>10} {:>8} {:>11}\n",
               "策略", "总收益", "年化", "最大回撤", "夏普", "交易数", "胜率");
    fmt::print("  买入持有       {:>+10.2f}%\n", bh_return * 100.0);
    print_row("单周期三买", m_single);
    print_row("多周期三买", m_multi);

    bt::write_result_csv(r_single, "outputs/单周期三买");
    bt::write_result_csv(r_multi, "outputs/多周期三买");
    bt::plot_backtest(r_single, bars, stock_code, "单周期三买", "outputs/单周期三买.png");
    bt::plot_backtest(r_multi, bars, stock_code, "多周期三买", "outputs/多周期三买.png");

    return 0;
}
