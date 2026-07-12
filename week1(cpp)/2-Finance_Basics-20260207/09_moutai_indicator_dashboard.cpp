// 对应 Python: 9-贵州茅台指标仪表盘.py
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
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
    const size_t SHOW_DAYS = 80;
    const size_t MA_PERIOD = 20;
    const size_t RSI_PERIOD = 14;
    const size_t ATR_PERIOD = 14;

    if (!fs::exists(DATA_FILE)) {
        fmt::print("错误：数据文件不存在 {}\n", DATA_FILE);
        return 1;
    }

    auto csv = quant::csv::read_csv(DATA_FILE);
    auto date_col = csv.column("date");
    auto close_col = csv.column_double("close");
    auto high_col = csv.column_double("high");
    auto low_col = csv.column_double("low");
    auto volume_col = csv.column_double("volume");

    if (date_col.empty() || close_col.empty() || high_col.empty() || low_col.empty() || volume_col.empty()) {
        fmt::print("错误：数据缺少必要列\n");
        return 1;
    }

    std::vector<std::pair<std::string, size_t>> indexed;
    for (size_t i = 0; i < date_col.size(); ++i) indexed.push_back({date_col[i], i});
    std::sort(indexed.begin(), indexed.end());

    std::vector<std::string> dates;
    std::vector<double> close, high, low, volume;
    for (const auto& [d, idx] : indexed) {
        dates.push_back(d);
        close.push_back(close_col[idx]);
        high.push_back(high_col[idx]);
        low.push_back(low_col[idx]);
        volume.push_back(volume_col[idx]);
    }

    size_t need = SHOW_DAYS + std::max({MA_PERIOD, RSI_PERIOD, ATR_PERIOD}) + 5;
    if (dates.size() > need) {
        size_t start = dates.size() - need;
        dates = std::vector<std::string>(dates.begin() + start, dates.end());
        close = std::vector<double>(close.begin() + start, close.end());
        high = std::vector<double>(high.begin() + start, high.end());
        low = std::vector<double>(low.begin() + start, low.end());
        volume = std::vector<double>(volume.begin() + start, volume.end());
    }

    auto ma20 = quant::ind::ma(close, MA_PERIOD);
    auto rsi = quant::ind::rsi(close, RSI_PERIOD);
    auto atr = quant::ind::atr(high, low, close, ATR_PERIOD);

    // Find valid range (skip NaNs)
    size_t first_valid = 0;
    for (size_t i = 0; i < close.size(); ++i) {
        if (std::isfinite(rsi[i]) && std::isfinite(atr[i])) {
            first_valid = i;
            break;
        }
    }

    std::vector<double> xs, close_v, ma_v, rsi_v, atr_v, vol_v;
    std::vector<std::string> dates_v;
    for (size_t i = first_valid; i < close.size(); ++i) {
        xs.push_back(static_cast<double>(i - first_valid));
        dates_v.push_back(dates[i]);
        close_v.push_back(close[i]);
        ma_v.push_back(ma20[i]);
        rsi_v.push_back(rsi[i]);
        atr_v.push_back(atr[i]);
        vol_v.push_back(volume[i] / 10000.0);
    }

    auto fig = figure(true);
    fig->size(1400, 1200);
    fig->title(fmt::format("{}({}) 四维指标仪表盘：趋势+震荡+能量+波动", STOCK_NAME, STOCK_CODE));

    auto ax1 = subplot(4, 1, 0);
    ax1->plot(xs, close_v, "b-")->line_width(1.2).display_name("收盘价");
    ax1->plot(xs, ma_v, "orange")->line_width(1.2).display_name(fmt::format("MA{}(趋势)", MA_PERIOD));
    ax1->ylabel("价格");
    ax1->title("趋势型：MA 方向");
    ax1->grid(on);
    ax1->legend();

    auto ax2 = subplot(4, 1, 1);
    ax2->plot(xs, rsi_v, "purple")->line_width(1.2).display_name(fmt::format("RSI({})", RSI_PERIOD));
    ax2->plot(xs, std::vector<double>(xs.size(), 80.0), "r--")->display_name("超买80");
    ax2->plot(xs, std::vector<double>(xs.size(), 20.0), "g--")->display_name("超卖20");
    ax2->plot(xs, std::vector<double>(xs.size(), 50.0), "k:")->display_name("中线");
    ax2->ylim({0.0, 100.0});
    ax2->ylabel("RSI");
    ax2->title("震荡型：RSI 位置（超买/超卖）");
    ax2->grid(on);
    ax2->legend();

    auto ax3 = subplot(4, 1, 2);
    std::vector<double> red_x, red_y, green_x, green_y;
    for (size_t i = 0; i < close_v.size(); ++i) {
        double prev = (i > 0) ? close_v[i - 1] : close_v[0];
        if (close_v[i] >= prev) { red_x.push_back(xs[i]); red_y.push_back(vol_v[i]); }
        else { green_x.push_back(xs[i]); green_y.push_back(vol_v[i]); }
    }
    if (!red_x.empty()) ax3->bar(red_x, red_y)->face_color("red").display_name("上涨量");
    if (!green_x.empty()) ax3->bar(green_x, green_y)->face_color("green").display_name("下跌量");
    ax3->ylabel("成交量（万）");
    ax3->title("能量型：成交量 燃料");
    ax3->grid(on);

    auto ax4 = subplot(4, 1, 3);
    ax4->plot(xs, atr_v, "brown")->line_width(1.2).display_name(fmt::format("ATR({})", ATR_PERIOD));
    ax4->ylabel("ATR");
    ax4->xlabel("日期");
    ax4->title("波动型：ATR 风险");
    ax4->grid(on);
    ax4->legend();

    fs::create_directories("outputs");
    fig->save("outputs/8_贵州茅台指标仪表盘.png");
    fmt::print("图表已保存：outputs/8_贵州茅台指标仪表盘.png\n");
    fmt::print("\n说明：不要用 MACD 和均线同时验证同一信号（同源）；应多维度：趋势+震荡+能量+波动。\n");

    return 0;
}
