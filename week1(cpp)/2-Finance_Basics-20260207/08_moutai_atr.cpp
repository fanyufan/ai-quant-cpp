// 对应 Python: 8-贵州茅台ATR指标计算.py
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <filesystem>
#include <fmt/format.h>
#include "csv.hpp"
#include "indicators.hpp"

namespace fs = std::filesystem;

int main() {
    const std::string STOCK_NAME = "贵州茅台";
    const std::string STOCK_CODE = "600519.SH";
    const std::string DATA_FILE = "data/600519_SH_daily.csv";
    const size_t ATR_PERIOD = 14;
    const size_t LOOKBACK_DAYS = 60;

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
    auto high_col = csv.column_double("high");
    auto low_col = csv.column_double("low");
    auto close_col = csv.column_double("close");
    if (date_col.empty() || high_col.empty() || low_col.empty() || close_col.empty()) {
        fmt::print("错误：数据缺少 date/high/low/close 列\n");
        return 1;
    }

    size_t take = std::min(LOOKBACK_DAYS + ATR_PERIOD, date_col.size());
    size_t start = date_col.size() - take;

    std::vector<std::string> dates;
    std::vector<double> high, low, close;
    for (size_t i = start; i < date_col.size(); ++i) {
        dates.push_back(date_col[i]);
        high.push_back(high_col[i]);
        low.push_back(low_col[i]);
        close.push_back(close_col[i]);
    }

    auto atr = quant::ind::atr(high, low, close, ATR_PERIOD);

    // Filter valid ATR values
    std::vector<std::string> valid_dates;
    std::vector<double> valid_close, valid_atr;
    for (size_t i = 0; i < atr.size(); ++i) {
        if (!std::isnan(atr[i])) {
            valid_dates.push_back(dates[i]);
            valid_close.push_back(close[i]);
            valid_atr.push_back(atr[i]);
        }
    }

    if (valid_atr.empty()) {
        fmt::print("错误：无法计算有效 ATR\n");
        return 1;
    }

    double last_atr = valid_atr.back();
    double last_close = valid_close.back();
    std::string last_date = valid_dates.back();
    double stop_loss_price = last_close - 2.0 * last_atr;

    fmt::print("ATR 周期：{} 日\n", ATR_PERIOD);
    fmt::print("最近一日：{}\n", last_date);
    fmt::print("收盘价：{:.2f} 元\n", last_close);
    fmt::print("ATR({})：{:.2f} 元（日均波动约 {:.2f} 元）\n", ATR_PERIOD, last_atr, last_atr);
    fmt::print("2*ATR 止损距离：{:.2f} 元\n", 2.0 * last_atr);
    fmt::print("若当日买入，2*ATR 止损价：{:.2f} 元（跌破则考虑止损）\n", stop_loss_price);
    fmt::print("{:-<60}\n", "");
    fmt::print("说明：波动大的标的仓位要小；止损常设为 2*ATR，海龟法则核心。\n");
    fmt::print("{:=<60}\n", "");

    fmt::print("\n最近 5 日 ATR 与 2*ATR 止损价：\n");
    for (int i = -5; i < 0; ++i) {
        size_t idx = valid_atr.size() + i;
        double c = valid_close[idx];
        double a = valid_atr[idx];
        double sl = c - 2.0 * a;
        fmt::print("  {}  收盘={:.2f}  ATR={:.2f}  2*ATR止损价={:.2f}\n",
                   valid_dates[idx], c, a, sl);
    }

    return 0;
}
