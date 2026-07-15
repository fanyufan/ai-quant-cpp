// 4-三类买卖点信号
// 对应 week5/9-缠论量化-20260314/CASE-缠论精华量化/4-三类买卖点信号.py

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <iostream>
#include <map>
#include <vector>

#include "backtest.hpp"
#include "backtest_data_mysql.hpp"
#include "chan_analyzer.hpp"
#include "env.hpp"

using namespace quant;

namespace {

size_t find_date_index(const std::vector<bt::Bar>& bars, const std::string& date) {
    for (size_t i = 0; i < bars.size(); ++i) {
        if (bars[i].date >= date) return i;
    }
    return bars.size();
}

double future_return(const std::vector<bt::Bar>& bars, size_t idx, int days) {
    if (idx >= bars.size()) return std::numeric_limits<double>::quiet_NaN();
    size_t target = idx + static_cast<size_t>(days);
    if (target >= bars.size()) target = bars.size() - 1;
    return bars[target].close / bars[idx].close - 1.0;
}

const char* signal_name(chan::SignalType t) {
    switch (t) {
        case chan::SignalType::FirstBuy: return "一买";
        case chan::SignalType::SecondBuy: return "二买";
        case chan::SignalType::ThirdBuy: return "三买";
        case chan::SignalType::ThirdSell: return "三卖";
    }
    return "";
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    const std::string stock_code = "600519.SH";
    const std::string start_date = "2023-01-01";
    const std::string end_date = "2025-12-31";

    auto bars = bt::data::load_from_mysql(cfg, stock_code, start_date, end_date);
    if (bars.empty()) {
        fmt::print("未找到 {} 的数据\n", stock_code);
        return 1;
    }

    fmt::print("\n{0}\n", std::string(60, '='));
    fmt::print("三类买卖点信号分析 | {}\n", stock_code);
    fmt::print("{0}\n", std::string(60, '='));

    chan::ChanAnalyzer analyzer(bars);
    analyzer.analyze();
    analyzer.summary();

    const auto& signals = analyzer.signals();
    fmt::print("\n信号后续收益分析:\n");
    fmt::print("  {:<10} {:>8} {:>8} {:>8} {:>8}\n", "类型", "5日", "10日", "20日", "40日");

    std::map<chan::SignalType, std::vector<double>> returns_10d;
    std::map<chan::SignalType, int> win_count, total_count;
    for (const auto& s : signals) {
        size_t idx = find_date_index(bars, s.date);
        double r5 = future_return(bars, idx, 5) * 100.0;
        double r10 = future_return(bars, idx, 10) * 100.0;
        double r20 = future_return(bars, idx, 20) * 100.0;
        double r40 = future_return(bars, idx, 40) * 100.0;
        fmt::print("  {} {:>6.1f}% {:>6.1f}% {:>6.1f}% {:>6.1f}%\n",
                   signal_name(s.type), r5, r10, r20, r40);
        returns_10d[s.type].push_back(r10);
        total_count[s.type]++;
        if (r10 > 0) win_count[s.type]++;
    }

    fmt::print("\n按信号类型10日胜率:\n");
    for (const auto& kv : total_count) {
        int wins = win_count[kv.first];
        fmt::print("  {}: {}/{} = {:.1f}%\n", signal_name(kv.first), wins, kv.second,
                   100.0 * wins / kv.second);
    }

    // Third buy detailed analysis.
    fmt::print("\n三买信号详细分析:\n");
    fmt::print("  {:<10} {:>8} {:>8} {:>8} {:>8} {:>8} {:>10}\n",
               "日期", "距ZG%", "5日", "10日", "20日", "40日", "评价");
    for (const auto& s : signals) {
        if (s.type != chan::SignalType::ThirdBuy) continue;
        size_t idx = find_date_index(bars, s.date);
        double price = bars[idx].close;
        double dist_zg = (s.zhongshu_zg > 0.0) ? (price / s.zhongshu_zg - 1.0) * 100.0 : 0.0;
        double r5 = future_return(bars, idx, 5) * 100.0;
        double r10 = future_return(bars, idx, 10) * 100.0;
        double r20 = future_return(bars, idx, 20) * 100.0;
        double r40 = future_return(bars, idx, 40) * 100.0;
        std::string eval;
        if (r20 >= 10.0) eval = "强势";
        else if (r20 >= 0.0) eval = "有效";
        else eval = "失败";
        fmt::print("  {} {:>+7.2f}% {:>+6.1f}% {:>+6.1f}% {:>+6.1f}% {:>+6.1f}% {:>10}\n",
                   s.date, dist_zg, r5, r10, r20, r40, eval);
    }

    analyzer.plot("outputs/4-三类买卖点.png", "三类买卖点", true, true, true, true, false);
    return 0;
}
