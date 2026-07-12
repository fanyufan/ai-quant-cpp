// 对应 Python: 6-贵州茅台MACD交易信号.py
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
    const size_t SHORT_PERIOD = 12;
    const size_t LONG_PERIOD = 26;
    const size_t SIGNAL_PERIOD = 9;
    const size_t SHOW_DAYS = 150;

    if (!fs::exists(DATA_FILE)) {
        fmt::print("错误：数据文件不存在 {}\n", DATA_FILE);
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

    size_t take = std::min(SHOW_DAYS + LONG_PERIOD + SIGNAL_PERIOD, date_col.size());
    size_t start = date_col.size() - take;

    std::vector<std::string> dates;
    std::vector<double> close;
    for (size_t i = start; i < date_col.size(); ++i) {
        dates.push_back(date_col[i]);
        close.push_back(close_col[i]);
    }

    auto macd = quant::ind::macd(close, SHORT_PERIOD, LONG_PERIOD, SIGNAL_PERIOD);

    // Find golden/death crosses
    std::vector<size_t> golden_idx, death_idx;
    for (size_t i = 1; i < macd.dif.size(); ++i) {
        if (std::isnan(macd.dif[i - 1]) || std::isnan(macd.dea[i - 1]) ||
            std::isnan(macd.dif[i]) || std::isnan(macd.dea[i])) {
            continue;
        }
        if (macd.dif[i - 1] <= macd.dea[i - 1] && macd.dif[i] > macd.dea[i]) {
            golden_idx.push_back(i);
        }
        if (macd.dif[i - 1] >= macd.dea[i - 1] && macd.dif[i] < macd.dea[i]) {
            death_idx.push_back(i);
        }
    }

    // Find divergence candidates: positive bar but shorter than previous
    std::vector<size_t> divergence_idx;
    for (size_t i = 2; i < macd.bar.size(); ++i) {
        if (macd.bar[i] > 0 && macd.bar[i - 1] > 0 && macd.bar[i] < macd.bar[i - 1]) {
            divergence_idx.push_back(i);
        }
    }

    // Plot
    auto fig = figure(false);
    fig->size(1400, 800);

    std::vector<double> xs(close.size());
    for (size_t i = 0; i < close.size(); ++i) xs[i] = static_cast<double>(i);

    // Subplot 1: price
    auto ax1 = subplot(2, 1, 0);
    ax1->plot(xs, close, "b-")->line_width(1.2).display_name("收盘价");

    std::vector<double> gx, gy, dx, dy;
    for (auto i : golden_idx) { gx.push_back(xs[i]); gy.push_back(close[i]); }
    for (auto i : death_idx) { dx.push_back(xs[i]); dy.push_back(close[i]); }

    if (!gx.empty()) {
        ax1->scatter(gx, gy, 8.0)->marker("^").color("red").display_name("金叉");
    }
    if (!dx.empty()) {
        ax1->scatter(dx, dy, 8.0)->marker("v").color("green").display_name("死叉");
    }

    ax1->ylabel("价格 (元)");
    ax1->title("收盘价与买卖点");
    ax1->grid(on);
    ax1->legend();

    // Subplot 2: MACD
    auto ax2 = subplot(2, 1, 1);
    ax2->plot(xs, macd.dif, "b-")->line_width(1.0).display_name("DIF");
    ax2->plot(xs, macd.dea, "orange")->line_width(1.0).display_name("DEA");

    // MACD bars with colors
    std::vector<double> red_x, red_y, green_x, green_y;
    for (size_t i = 0; i < macd.bar.size(); ++i) {
        if (macd.bar[i] >= 0) {
            red_x.push_back(xs[i]);
            red_y.push_back(macd.bar[i]);
        } else {
            green_x.push_back(xs[i]);
            green_y.push_back(macd.bar[i]);
        }
    }
    if (!red_x.empty()) {
        ax2->bar(red_x, red_y)->face_color("red").display_name("红柱");
    }
    if (!green_x.empty()) {
        ax2->bar(green_x, green_y)->face_color("green").display_name("绿柱");
    }

    ax2->plot(xs, std::vector<double>(xs.size(), 0.0), "k--")->display_name("零轴");
    ax2->ylabel("MACD");
    ax2->xlabel("日期");
    ax2->title(fmt::format("MACD({},{},{}) 红柱变短=背驰，需警惕", SHORT_PERIOD, LONG_PERIOD, SIGNAL_PERIOD));
    ax2->grid(on);
    ax2->legend();

    fs::create_directories("outputs");
    std::string out_path = "outputs/5-贵州茅台MACD交易信号.png";
    fig->save(out_path);

    fmt::print("图表已保存：{}\n", out_path);
    fmt::print("\n本区间金叉 {} 次，死叉 {} 次\n", golden_idx.size(), death_idx.size());
    fmt::print("课程要点：红柱变短为背驰，涨速放缓，量化中常用作风险信号。\n");

    return 0;
}
