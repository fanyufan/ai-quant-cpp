// K 线形态识别：扫描全量数据并统计看涨/看跌信号
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
    fmt::print("K 线形态识别统计\n");
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

    int bullish_count = 0;
    int bearish_count = 0;
    bool any_pattern = false;

    for (size_t i = 0; i < bars.size(); ++i) {
        auto patterns = quant::candle::scan(bars, i);
        if (patterns.empty()) continue;

        any_pattern = true;
        fmt::print("{}  收盘价：{:.2f}\n", bars[i].date, bars[i].close);
        for (const auto& [name, signal] : patterns) {
            const char* direction = (signal == 1) ? "看涨" : (signal == -1) ? "看跌" : "中性";
            fmt::print("    - {} [{}]\n", name, direction);
            if (signal == 1) ++bullish_count;
            else if (signal == -1) ++bearish_count;
        }
    }

    fmt::print("{:-<70}\n", "");
    if (!any_pattern) {
        fmt::print("未识别到任何 K 线形态\n");
    }
    fmt::print("看涨信号总数：{}\n", bullish_count);
    fmt::print("看跌信号总数：{}\n", bearish_count);
    fmt::print("净多空强度：{}\n", bullish_count - bearish_count);
    fmt::print("{:=<70}\n", "");

    return 0;
}
