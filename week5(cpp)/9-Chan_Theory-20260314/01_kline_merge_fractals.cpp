// 1-K线包含处理与分型识别
// 对应 week5/9-缠论量化-20260314/CASE-缠论精华量化/1-K线包含处理与分型识别.py

#include <fmt/format.h>
#include <iostream>
#include <vector>

#include "backtest.hpp"
#include "backtest_config.hpp"
#include "backtest_data.hpp"
#include "backtest_data_mysql.hpp"
#include "chan_analyzer.hpp"
#include "env.hpp"

using namespace quant;

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    const std::string stock_code = "600519.SH";
    const std::string start_date = "2025-06-01";
    const std::string end_date = "2025-12-31";

    auto bars = bt::data::load_from_mysql(cfg, stock_code, start_date, end_date);
    if (bars.empty()) {
        fmt::print("未找到 {} 的数据\n", stock_code);
        return 1;
    }

    fmt::print("\n{0}\n", std::string(60, '='));
    fmt::print("K线包含处理与分型识别 | {}\n", stock_code);
    fmt::print("{0}\n", std::string(60, '='));

    chan::ChanAnalyzer analyzer(bars);
    analyzer.analyze();

    const auto& raw = analyzer.raw_bars();
    const auto& merged = analyzer.merged_bars();
    const auto& fractals = analyzer.fractals();

    fmt::print("\n合并统计:\n");
    fmt::print("  原始K线数: {}\n", raw.size());
    fmt::print("  合并后K线数: {}\n", merged.size());
    fmt::print("  被合并K线数: {} ({:.1f}%)\n",
               raw.size() - merged.size(),
               100.0 * static_cast<double>(raw.size() - merged.size()) / raw.size());

    fmt::print("\n被合并K线示例 (前10):\n");
    size_t merged_examples = 0;
    for (size_t i = 0; i < raw.size() && merged_examples < 10; ++i) {
        // Find whether this raw date is a leftmost date of a merged bar.
        bool is_left = false;
        for (const auto& m : merged) {
            if (m.date == raw[i].date) { is_left = true; break; }
        }
        if (!is_left) {
            fmt::print("  {} 开{:.2f} 高{:.2f} 低{:.2f} 收{:.2f}\n",
                       raw[i].date, raw[i].open, raw[i].high, raw[i].low, raw[i].close);
            ++merged_examples;
        }
    }

    size_t top_count = 0, bottom_count = 0;
    for (const auto& f : fractals) {
        if (f.type == chan::FractalType::Top) ++top_count;
        else ++bottom_count;
    }
    fmt::print("\n合并后分型统计:\n");
    fmt::print("  顶分型: {} | 底分型: {} | 合计: {}\n", top_count, bottom_count, fractals.size());

    // Pseudo fractals on raw bars.
    size_t pseudo_top = 0, pseudo_bottom = 0;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        const auto& p = raw[i - 1];
        const auto& c = raw[i];
        const auto& n = raw[i + 1];
        if (c.high > p.high && c.high > n.high && c.low > p.low && c.low > n.low) ++pseudo_top;
        else if (c.low < p.low && c.low < n.low && c.high < p.high && c.high < n.high) ++pseudo_bottom;
    }
    fmt::print("\n原始K线伪分型统计:\n");
    fmt::print("  顶分型: {} | 底分型: {} | 合计: {}\n", pseudo_top, pseudo_bottom, pseudo_top + pseudo_bottom);
    fmt::print("  合并后分型 / 原始伪分型 = {:.1f}%\n",
               100.0 * static_cast<double>(fractals.size()) / static_cast<double>(pseudo_top + pseudo_bottom));

    analyzer.plot_compare_merge("outputs/1-分型识别_合并对比.png", "K线包含合并对比");
    analyzer.plot("outputs/1-分型识别_完整图.png", "分型识别", false, false, false, true, false);

    return 0;
}
