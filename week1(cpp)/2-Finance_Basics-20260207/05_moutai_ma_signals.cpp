// 对应 Python: 5-贵州茅台MA交易信号.py
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <filesystem>
#include <fmt/format.h>
#include <matplot/matplot.h>
#include "csv.hpp"
#include "indicators.hpp"

namespace fs = std::filesystem;
using namespace matplot;

int main() {
    const std::string STOCK_NAME = "贵州茅台";
    const std::string STOCK_CODE = "600519.SH";
    const std::string DATA_FILE = "data/600519_SH_daily.csv";
    const size_t MA_SHORT = 5;
    const size_t MA_LONG = 20;
    const size_t SHOW_DAYS = 120;

    if (!fs::exists(DATA_FILE)) {
        fmt::print("错误：数据文件不存在 {}\n", DATA_FILE);
        fmt::print("请先运行数据下载脚本。\n");
        return 1;
    }

    auto csv = quant::csv::read_csv(DATA_FILE);
    if (csv.empty()) {
        fmt::print("错误：无法读取 {}\n", DATA_FILE);
        return 1;
    }

    auto date_col = csv.column("date");
    auto close_col = csv.column_double("close");
    if (date_col.empty() || close_col.empty()) {
        fmt::print("错误：数据缺少 date 或 close 列\n");
        return 1;
    }

    // Sort by date (assume CSV already sorted, but take last N rows)
    size_t take = std::min(SHOW_DAYS + MA_LONG, date_col.size());
    size_t start = date_col.size() - take;

    std::vector<std::string> dates;
    std::vector<double> close;
    for (size_t i = start; i < date_col.size(); ++i) {
        dates.push_back(date_col[i]);
        close.push_back(close_col[i]);
    }

    auto ma5 = quant::ind::sma(close, MA_SHORT);
    auto ma20 = quant::ind::sma(close, MA_LONG);

    // Find golden/death crosses
    std::vector<size_t> golden_idx, death_idx;
    for (size_t i = MA_LONG; i < close.size(); ++i) {
        if (std::isnan(ma5[i - 1]) || std::isnan(ma20[i - 1]) ||
            std::isnan(ma5[i]) || std::isnan(ma20[i])) {
            continue;
        }
        if (ma5[i - 1] <= ma20[i - 1] && ma5[i] > ma20[i]) {
            golden_idx.push_back(i);
        }
        if (ma5[i - 1] >= ma20[i - 1] && ma5[i] < ma20[i]) {
            death_idx.push_back(i);
        }
    }

    // Plot
    auto fig = figure(false);
    fig->size(1400, 600);
    auto ax = fig->current_axes();

    std::vector<double> xs(close.size());
    for (size_t i = 0; i < close.size(); ++i) xs[i] = static_cast<double>(i);

    ax->hold(on);
    ax->plot(xs, close, "b-")->line_width(1.2).display_name("收盘价");
    ax->plot(xs, ma5, "orange")->line_width(1.2).display_name(fmt::format("MA{}", MA_SHORT));
    ax->plot(xs, ma20, "green")->line_width(1.2).display_name(fmt::format("MA{}", MA_LONG));

    std::vector<double> gx, gy, dx, dy;
    for (auto i : golden_idx) { gx.push_back(xs[i]); gy.push_back(close[i]); }
    for (auto i : death_idx) { dx.push_back(xs[i]); dy.push_back(close[i]); }

    if (!gx.empty()) {
        ax->scatter(gx, gy, 10.0)->marker("^").color("red");
    }
    if (!dx.empty()) {
        ax->scatter(dx, dy, 10.0)->marker("v").color("green");
    }

    ax->xlabel("日期");
    ax->ylabel("价格 (元)");
    ax->title(fmt::format("{}({}) MA{}/MA{} 金叉死叉", STOCK_NAME, STOCK_CODE, MA_SHORT, MA_LONG));
    ax->grid(on);
    ax->legend();

    fs::create_directories("outputs");
    std::string out_path = "outputs/4-贵州茅台MA交易信号.png";
    fig->save(out_path);
    fmt::print("图表已保存：{}\n", out_path);
    fmt::print("\n本区间金叉次数：{}，死叉次数：{}\n", golden_idx.size(), death_idx.size());
    fmt::print("说明：金叉偏多、死叉偏空；回踩 MA20 不破可视为支撑。\n");

    return 0;
}
