// 技术指标基础：最新指标值与 K 线形态扫描
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "backtest.hpp"
#include "backtest_data.hpp"
#include "candlestick.hpp"
#include "indicators.hpp"

namespace fs = std::filesystem;

struct CliArgs {
    std::string stock = "600519.SH";
    std::string start = "2025-01-01";
    std::string end = "2025-12-31";
    std::string data_file = "data/600519_SH_daily.csv";
    std::string output_dir = "outputs";
};

static CliArgs parse_args(int argc, char* argv[]) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (i + 1 >= argc) break;
        std::string value = argv[++i];
        if (key == "--stock") args.stock = value;
        else if (key == "--start") args.start = value;
        else if (key == "--end") args.end = value;
        else if (key == "--data-file") args.data_file = value;
        else if (key == "--output-dir") args.output_dir = value;
    }
    return args;
}

static std::string stock_name(const std::string& code) {
    static const std::unordered_map<std::string, std::string> names = {
        {"600519.SH", "贵州茅台"},
        {"688256.SH", "寒武纪"},
    };
    auto it = names.find(code);
    return (it != names.end()) ? it->second : code;
}

int main(int argc, char* argv[]) {
    const auto args = parse_args(argc, argv);

    fmt::print("{:=<70}\n", "");
    fmt::print("技术指标基础面板\n");
    fmt::print("{:=<70}\n", "");
    fmt::print("股票：{}({})\n", stock_name(args.stock), args.stock);
    fmt::print("数据区间：{} 至 {}\n", args.start, args.end);
    fmt::print("数据文件：{}\n", args.data_file);
    fmt::print("{:-<70}\n", "");

    if (!fs::exists(args.data_file)) {
        fmt::print("错误：数据文件不存在：{}\n", args.data_file);
        return 1;
    }

    auto bars = quant::bt::data::load_from_csv(args.data_file, args.start, args.end);
    if (bars.empty()) {
        fmt::print("错误：区间 {} 至 {} 内没有数据\n", args.start, args.end);
        return 1;
    }

    std::vector<double> close;
    std::vector<double> high;
    std::vector<double> low;
    close.reserve(bars.size());
    high.reserve(bars.size());
    low.reserve(bars.size());
    for (const auto& b : bars) {
        close.push_back(b.close);
        high.push_back(b.high);
        low.push_back(b.low);
    }

    auto sma20 = quant::ind::sma(close, 20);
    auto ema20 = quant::ind::ema(close, 20);
    auto rsi14 = quant::ind::rsi(close, 14);
    auto macd = quant::ind::macd(close, 12, 26, 9);
    auto atr14 = quant::ind::atr(high, low, close, 14);
    auto bb = quant::ind::bollinger(close, 20, 2.0);
    auto adx = quant::ind::adx(high, low, close, 14);
    auto bias6 = quant::ind::bias(close, 6);
    auto roc10 = quant::ind::roc(close, 10);

    const size_t last = bars.size() - 1;

    auto fmt_val = [](double v) -> std::string {
        if (!std::isfinite(v)) return "NaN";
        return fmt::format("{:.4f}", v);
    };

    fmt::print("最新交易日：{}  收盘价：{:.2f}\n", bars[last].date, bars[last].close);
    fmt::print("\n最新指标数值：\n");
    fmt::print("{:-<50}\n", "");
    fmt::print("  {:<20} {}\n", "SMA(20)", fmt_val(sma20[last]));
    fmt::print("  {:<20} {}\n", "EMA(20)", fmt_val(ema20[last]));
    fmt::print("  {:<20} {}\n", "RSI(14)", fmt_val(rsi14[last]));
    fmt::print("  {:<20} {}\n", "MACD DIF", fmt_val(macd.dif[last]));
    fmt::print("  {:<20} {}\n", "MACD DEA", fmt_val(macd.dea[last]));
    fmt::print("  {:<20} {}\n", "MACD BAR", fmt_val(macd.bar[last]));
    fmt::print("  {:<20} {}\n", "ATR(14)", fmt_val(atr14[last]));
    fmt::print("  {:<20} {}\n", "BBANDS UPPER", fmt_val(bb.upper[last]));
    fmt::print("  {:<20} {}\n", "BBANDS MIDDLE", fmt_val(bb.middle[last]));
    fmt::print("  {:<20} {}\n", "BBANDS LOWER", fmt_val(bb.lower[last]));
    fmt::print("  {:<20} {}\n", "ADX(14)", fmt_val(adx.adx[last]));
    fmt::print("  {:<20} {}\n", "BIAS(6)", fmt_val(bias6[last]));
    fmt::print("  {:<20} {}\n", "ROC(10)", fmt_val(roc10[last]));
    fmt::print("{:-<50}\n", "");

    const size_t show_days = 5;
    size_t start = (bars.size() > show_days) ? bars.size() - show_days : 0;

    fmt::print("\n最近 {} 个交易日 K 线形态扫描：\n", show_days);
    fmt::print("{:-<70}\n", "");
    bool any_pattern = false;
    for (size_t i = start; i < bars.size(); ++i) {
        auto patterns = quant::candle::scan(bars, i);
        if (patterns.empty()) continue;
        any_pattern = true;
        fmt::print("  {}  收盘价：{:.2f}\n", bars[i].date, bars[i].close);
        for (const auto& [name, signal] : patterns) {
            const char* direction = (signal == 1) ? "看涨" : (signal == -1) ? "看跌" : "中性";
            fmt::print("    - {} [{}]\n", name, direction);
        }
    }
    if (!any_pattern) {
        fmt::print("  最近 {} 个交易日未识别到显著 K 线形态\n", show_days);
    }
    fmt::print("{:-<70}\n", "");

    return 0;
}
