// 指标对比：原生 C++ 实现 vs 课程 Python/Talib 输出
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "backtest.hpp"
#include "backtest_data.hpp"
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

    fmt::print("{:=<80}\n", "");
    fmt::print("指标对比：原生 C++ 实现 vs 课程 Python/Talib 输出\n");
    fmt::print("{:=<80}\n", "");
    fmt::print("股票：{}({})\n", stock_name(args.stock), args.stock);
    fmt::print("数据区间：{} 至 {}\n", args.start, args.end);
    fmt::print("数据文件：{}\n", args.data_file);
    fmt::print("{:-<80}\n", "");

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

    const size_t ma_period = 20;
    const size_t rsi_period = 14;
    const size_t atr_period = 14;
    const size_t bb_period = 20;
    const double bb_k = 2.0;

    auto sma20 = quant::ind::sma(close, ma_period);
    auto ema20 = quant::ind::ema(close, ma_period);
    auto rsi14 = quant::ind::rsi(close, rsi_period);
    auto macd = quant::ind::macd(close, 12, 26, 9);
    auto atr14 = quant::ind::atr(high, low, close, atr_period);
    auto bb = quant::ind::bollinger(close, bb_period, bb_k);

    const size_t rows_to_show = 5;
    size_t start = (bars.size() > rows_to_show) ? bars.size() - rows_to_show : 0;

    fmt::print("最近 {} 个交易日的指标对比\n", rows_to_show);
    fmt::print("{:-<120}\n", "");
    fmt::print("{:>12} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10} {:>10}\n",
               "date", "close", "SMA(20)", "EMA(20)", "RSI(14)", "DIF", "DEA", "BAR", "ATR(14)", "BB_UPPER", "BB_MID", "BB_LOWER");
    fmt::print("{:-<120}\n", "");

    for (size_t i = start; i < bars.size(); ++i) {
        auto fmt_val = [](double v) -> std::string {
            if (!std::isfinite(v)) return "     NaN";
            return fmt::format("{:>10.2f}", v);
        };
        fmt::print("{:>12} {} {} {} {} {} {} {} {} {} {} {}\n",
                   bars[i].date,
                   fmt_val(bars[i].close),
                   fmt_val(sma20[i]),
                   fmt_val(ema20[i]),
                   fmt_val(rsi14[i]),
                   fmt_val(macd.dif[i]),
                   fmt_val(macd.dea[i]),
                   fmt_val(macd.bar[i]),
                   fmt_val(atr14[i]),
                   fmt_val(bb.upper[i]),
                   fmt_val(bb.middle[i]),
                   fmt_val(bb.lower[i]));
    }
    fmt::print("{:-<120}\n", "");

    return 0;
}
