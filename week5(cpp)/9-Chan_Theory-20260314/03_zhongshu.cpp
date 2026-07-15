// 3-中枢识别与可视化
// 对应 week5/9-缠论量化-20260314/CASE-缠论精华量化/3-中枢识别与可视化.py

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
    fmt::print("中枢识别与可视化 | {}\n", stock_code);
    fmt::print("{0}\n", std::string(60, '='));

    chan::ChanAnalyzer analyzer(bars);
    analyzer.analyze();

    const auto& bi_list = analyzer.bi_list();
    const auto& zhongshu_list = analyzer.zhongshu_list();

    fmt::print("\n笔数: {} | 中枢数: {}\n", bi_list.size(), zhongshu_list.size());

    if (!zhongshu_list.empty()) {
        fmt::print("\n中枢列表:\n");
        double total_amp = 0.0;
        int total_bis = 0;
        for (size_t i = 0; i < zhongshu_list.size(); ++i) {
            const auto& zs = zhongshu_list[i];
            double amp_pct = (zs.zg - zs.zd) / zs.center * 100.0;
            total_amp += amp_pct;
            total_bis += zs.bi_count;
            fmt::print("  [{}] {} ~ {} | ZG={:.2f} ZD={:.2f} 中心={:.2f} 幅度={:.2f}% 包含{}笔\n",
                       i + 1, zs.start_date, zs.end_date, zs.zg, zs.zd, zs.center, amp_pct, zs.bi_count);
        }

        fmt::print("\n中枢方向分析:\n");
        for (size_t i = 1; i < zhongshu_list.size(); ++i) {
            const auto& prev = zhongshu_list[i - 1];
            const auto& cur = zhongshu_list[i];
            std::string direction;
            if (cur.zg > prev.zg && cur.zd > prev.zd) direction = "上移";
            else if (cur.zg < prev.zg && cur.zd < prev.zd) direction = "下移";
            else direction = "重叠";
            fmt::print("  中枢{} -> 中枢{}: {}\n", i, i + 1, direction);
        }

        fmt::print("\n中枢统计:\n");
        fmt::print("  平均中枢幅度: {:.2f}%\n", total_amp / zhongshu_list.size());
        fmt::print("  平均包含笔数: {:.1f}\n", static_cast<double>(total_bis) / zhongshu_list.size());
    }

    analyzer.plot("outputs/3-中枢识别.png", "中枢识别", true, true, false, true, false);
    return 0;
}
