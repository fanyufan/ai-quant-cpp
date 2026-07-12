// 对应 Python: 7-贵州茅台RSI指标计算.py
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
    const size_t RSI_PERIOD = 14;
    const size_t SHOW_DAYS = 120;
    const double OVERBOUGHT = 80.0;
    const double OVERSOLD = 20.0;

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

    size_t take = std::min(SHOW_DAYS + RSI_PERIOD + 5, date_col.size());
    size_t start = date_col.size() - take;

    std::vector<std::string> dates;
    std::vector<double> close;
    for (size_t i = start; i < date_col.size(); ++i) {
        dates.push_back(date_col[i]);
        close.push_back(close_col[i]);
    }

    auto rsi = quant::ind::rsi(close, RSI_PERIOD);

    // Filter valid RSI values
    std::vector<std::string> valid_dates;
    std::vector<double> valid_close, valid_rsi;
    for (size_t i = 0; i < rsi.size(); ++i) {
        if (!std::isnan(rsi[i])) {
            valid_dates.push_back(dates[i]);
            valid_close.push_back(close[i]);
            valid_rsi.push_back(rsi[i]);
        }
    }

    // Plot
    auto fig = figure(false);
    fig->size(1400, 800);
    fig->title(fmt::format("{}({}) RSI({}) 超买超卖示意", STOCK_NAME, STOCK_CODE, RSI_PERIOD));

    std::vector<double> xs(valid_close.size());
    for (size_t i = 0; i < valid_close.size(); ++i) xs[i] = static_cast<double>(i);

    // Subplot 1: close price
    auto ax1 = subplot(2, 1, 0);
    ax1->plot(xs, valid_close, "b-")->line_width(1.2).display_name("收盘价");
    ax1->ylabel("价格 (元)");
    ax1->title("收盘价");
    ax1->grid(on);
    ax1->legend();

    // Subplot 2: RSI
    auto ax2 = subplot(2, 1, 1);
    ax2->plot(xs, valid_rsi, "purple")->line_width(1.2).display_name(fmt::format("RSI({})", RSI_PERIOD));
    ax2->plot(xs, std::vector<double>(valid_rsi.size(), OVERBOUGHT), "r--")->display_name(fmt::format("超买 {}", OVERBOUGHT));
    ax2->plot(xs, std::vector<double>(valid_rsi.size(), OVERSOLD), "g--")->display_name(fmt::format("超卖 {}", OVERSOLD));
    ax2->plot(xs, std::vector<double>(valid_rsi.size(), 50.0), "k:")->display_name("50");

    std::vector<double> overbought_x, overbought_y, oversold_x, oversold_y;
    for (size_t i = 0; i < valid_rsi.size(); ++i) {
        if (valid_rsi[i] >= OVERBOUGHT) {
            overbought_x.push_back(xs[i]);
            overbought_y.push_back(valid_rsi[i]);
        }
        if (valid_rsi[i] <= OVERSOLD) {
            oversold_x.push_back(xs[i]);
            oversold_y.push_back(valid_rsi[i]);
        }
    }
    if (!overbought_x.empty()) {
        ax2->scatter(overbought_x, overbought_y, 8.0)->color("red").display_name("超买日");
    }
    if (!oversold_x.empty()) {
        ax2->scatter(oversold_x, oversold_y, 8.0)->color("green").display_name("超卖日");
    }

    ax2->ylim({0.0, 100.0});
    ax2->ylabel("RSI");
    ax2->xlabel("日期");
    ax2->title(fmt::format("RSI：>={} 超买（考虑减仓/网格卖），<={} 超卖（考虑加仓/网格买）", OVERBOUGHT, OVERSOLD));
    ax2->grid(on);
    ax2->legend();

    fs::create_directories("outputs");
    std::string out_path = "outputs/6-贵州茅台RSI指标计算.png";
    fig->save(out_path);

    size_t oversold_count = oversold_x.size();
    size_t overbought_count = overbought_x.size();
    fmt::print("\n本区间内 RSI<={} 超卖天数: {}\n", OVERSOLD, oversold_count);
    fmt::print("本区间内 RSI>={} 超买天数: {}\n", OVERBOUGHT, overbought_count);
    fmt::print("课程要点：RSI<20 时反弹概率大，可作为网格策略的买入参考；RSI>80 时可考虑减仓或网格卖出。\n");
    fmt::print("\n图表已保存：{}\n", out_path);

    return 0;
}
