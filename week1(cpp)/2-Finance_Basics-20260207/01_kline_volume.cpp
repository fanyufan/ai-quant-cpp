// 对应 Python: 1-K线图与成交量.py
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <matplot/matplot.h>
#include "csv.hpp"

namespace fs = std::filesystem;
using namespace matplot;

int main() {
    const std::string STOCK_NAME = "贵州茅台";
    const std::string STOCK_CODE = "600519.SH";
    const std::string DATA_FILE = "data/600519_SH_daily.csv";
    const size_t SHOW_DAYS = 60;

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
    auto open_col = csv.column_double("open");
    auto high_col = csv.column_double("high");
    auto low_col = csv.column_double("low");
    auto close_col = csv.column_double("close");
    auto volume_col = csv.column_double("volume");
    if (date_col.empty() || open_col.empty() || high_col.empty() ||
        low_col.empty() || close_col.empty() || volume_col.empty()) {
        fmt::print("错误：数据缺少 open/high/low/close/volume 列\n");
        return 1;
    }

    size_t take = std::min(SHOW_DAYS, date_col.size());
    size_t start = date_col.size() - take;

    std::vector<std::string> dates;
    std::vector<double> open_p, high_p, low_p, close_p, volume;
    for (size_t i = start; i < date_col.size(); ++i) {
        dates.push_back(date_col[i]);
        open_p.push_back(open_col[i]);
        high_p.push_back(high_col[i]);
        low_p.push_back(low_col[i]);
        close_p.push_back(close_col[i]);
        volume.push_back(volume_col[i]);
    }

    auto fig = figure(false);
    fig->size(1400, 800);
    fig->title(fmt::format("{}({}) K线图与成交量", STOCK_NAME, STOCK_CODE));

    std::vector<double> xs(dates.size());
    for (size_t i = 0; i < dates.size(); ++i) xs[i] = static_cast<double>(i);

    // Subplot 1: candlestick
    auto ax1 = subplot(2, 1, 0);
    ax1->hold(on);
    for (size_t i = 0; i < dates.size(); ++i) {
        double o = open_p[i], h = high_p[i], l = low_p[i], c = close_p[i];
        std::string color = (c >= o) ? "red" : "green";
        double body_bottom = (c >= o) ? o : c;
        double body_height = std::abs(c - o);
        if (body_height < 1e-6) body_height = (h - l) * 0.02;

        // Body
        ax1->rectangle(xs[i] - 0.3, body_bottom, 0.6, body_height)
            ->color(color);
        // Upper wick
        ax1->plot({xs[i], xs[i]}, {std::max(o, c), h}, color);
        // Lower wick
        ax1->plot({xs[i], xs[i]}, {std::min(o, c), l}, color);
    }
    ax1->xlim({-0.5, static_cast<double>(dates.size()) - 0.5});
    ax1->ylabel("价格 (元)");
    ax1->title("K线：开盘/最高/最低/收盘（红=阳线 绿=阴线）");
    ax1->grid(on);

    // Subplot 2: volume
    auto ax2 = subplot(2, 1, 1);
    std::vector<double> red_x, red_y, green_x, green_y;
    for (size_t i = 0; i < dates.size(); ++i) {
        if (close_p[i] >= open_p[i]) {
            red_x.push_back(xs[i]);
            red_y.push_back(volume[i] / 1e4);
        } else {
            green_x.push_back(xs[i]);
            green_y.push_back(volume[i] / 1e4);
        }
    }
    ax2->bar(red_x, red_y)->face_color("red").display_name("上涨");
    ax2->bar(green_x, green_y)->face_color("green").display_name("下跌");
    ax2->ylabel("成交量 (万手)");
    ax2->xlabel("日期");
    ax2->title("成交量：上涨日红柱、下跌日绿柱，量价配合更健康");
    ax2->grid(on);
    ax2->legend();

    fs::create_directories("outputs");
    std::string out_path = "outputs/01_kline_volume_demo.png";
    fig->save(out_path);

    fmt::print("\n提示：若出现「价格创新高、成交量却缩小」的几天，多为量价背离，需警惕见顶。\n");
    fmt::print("可在图中自行观察最近是否出现该现象。\n");
    fmt::print("\n图表已保存：{}\n", out_path);

    return 0;
}
