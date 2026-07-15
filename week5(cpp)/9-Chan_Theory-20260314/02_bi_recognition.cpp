// 2-笔的自动化识别
// 对应 week5/9-缠论量化-20260314/CASE-缠论精华量化/2-笔的自动化识别.py

#include <fmt/format.h>
#include <iostream>
#include <vector>

#include "backtest.hpp"
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
    fmt::print("笔的自动化识别 | {}\n", stock_code);
    fmt::print("{0}\n", std::string(60, '='));

    chan::ChanAnalyzer analyzer(bars);
    analyzer.analyze();

    const auto& fractals = analyzer.confirmed_fractals();
    const auto& bi_list = analyzer.bi_list();

    fmt::print("\n确认分型数: {}\n", fractals.size());
    fmt::print("总笔数: {}\n", bi_list.size());

    struct Stats {
        int count = 0;
        double total_amp = 0.0;
        double total_return = 0.0;
        double max_return = 0.0;
        double total_k = 0.0;
    } up, down, all;

    for (const auto& bi : bi_list) {
        double amp = std::abs(bi.end_price - bi.start_price);
        double ret = (bi.end_price / bi.start_price - 1.0) * 100.0;
        int k = static_cast<int>(bi.end_idx - bi.start_idx);
        Stats* s = bi.up ? &up : &down;
        s->count += 1;
        s->total_amp += amp;
        s->total_return += ret;
        s->total_k += k;
        if (std::abs(ret) > s->max_return) s->max_return = std::abs(ret);
        all.count += 1;
        all.total_amp += amp;
        all.total_return += ret;
        all.total_k += k;
        if (std::abs(ret) > all.max_return) all.max_return = std::abs(ret);
    }

    auto print_stats = [](const std::string& name, const Stats& s) {
        if (s.count == 0) {
            fmt::print("  {:4} 数量: 0\n", name);
            return;
        }
        fmt::print("  {:4} 数量: {:3} | 平均幅度: {:6.2f} | 平均涨跌幅: {:+6.2f}% | 最大涨跌幅: {:+6.2f}% | 平均K线数: {:5.1f}\n",
                   name, s.count, s.total_amp / s.count, s.total_return / s.count,
                   s.max_return, s.total_k / s.count);
    };

    fmt::print("\n笔统计:\n");
    print_stats("上升", up);
    print_stats("下降", down);
    print_stats("整体", all);

    fmt::print("\n最近15笔:\n");
    size_t start = bi_list.size() > 15 ? bi_list.size() - 15 : 0;
    for (size_t i = start; i < bi_list.size(); ++i) {
        const auto& bi = bi_list[i];
        fmt::print("  [{}] {} -> {} | {} | 幅度: {:.2f}\n",
                   i + 1, bi.start_date, bi.end_date,
                   bi.up ? "上升" : "下降",
                   std::abs(bi.end_price - bi.start_price));
    }

    analyzer.plot("outputs/2-笔的识别.png", "笔的识别", true, false, false, true, false);
    return 0;
}
