// MACD 跟踪止损策略：金叉买入，最高价回撤触发止损或死叉离场
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
#include "indicators.hpp"

namespace fs = std::filesystem;

struct CliArgs {
    std::string stock = "600519.SH";
    std::string start = "2025-01-01";
    std::string end = "2025-12-31";
    std::string data_file = "data/600519_SH_daily.csv";
    std::string output_dir = "outputs";
    double trailing_stop = 0.08;
};

static CliArgs parse_args(int argc, char* argv[]) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (key == "--trailing-stop") {
            if (i + 1 < argc) args.trailing_stop = std::atof(argv[++i]);
        } else if (i + 1 < argc) {
            std::string value = argv[++i];
            if (key == "--stock") args.stock = value;
            else if (key == "--start") args.start = value;
            else if (key == "--end") args.end = value;
            else if (key == "--data-file") args.data_file = value;
            else if (key == "--output-dir") args.output_dir = value;
        }
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

class MacdTrailingStopStrategy : public quant::bt::Strategy {
public:
    explicit MacdTrailingStopStrategy(double trailing_stop)
        : trailing_stop_(trailing_stop), highest_since_entry_(0.0), in_position_(false) {}

    void init(const std::vector<quant::bt::Bar>& bars) override {
        std::vector<double> close;
        close.reserve(bars.size());
        for (const auto& b : bars) close.push_back(b.close);
        macd_ = quant::ind::macd(close, short_, long_, signal_);
    }

    void next(size_t idx, const std::vector<quant::bt::Bar>& bars, quant::bt::Broker& broker) override {
        if (idx == 0 || idx >= macd_.dif.size()) return;
        if (!std::isfinite(macd_.dif[idx - 1]) || !std::isfinite(macd_.dea[idx - 1]) ||
            !std::isfinite(macd_.dif[idx]) || !std::isfinite(macd_.dea[idx])) {
            return;
        }

        bool golden_cross = macd_.dif[idx - 1] <= macd_.dea[idx - 1] && macd_.dif[idx] > macd_.dea[idx];
        bool death_cross  = macd_.dif[idx - 1] >= macd_.dea[idx - 1] && macd_.dif[idx] < macd_.dea[idx];

        if (broker.shares() > 0) {
            if (!in_position_) {
                in_position_ = true;
                highest_since_entry_ = bars[idx].close;
            } else {
                highest_since_entry_ = std::max(highest_since_entry_, bars[idx].close);
            }

            double stop_price = highest_since_entry_ * (1.0 - trailing_stop_);
            bool hit_stop = bars[idx].close < stop_price;

            if (hit_stop || death_cross) {
                broker.sell(idx, bars);
                in_position_ = false;
                highest_since_entry_ = 0.0;
            }
        } else {
            in_position_ = false;
            highest_since_entry_ = 0.0;

            if (golden_cross) {
                broker.buy(idx, bars);
                in_position_ = true;
                highest_since_entry_ = bars[idx].close;
            }
        }
    }

private:
    static constexpr size_t short_ = 12;
    static constexpr size_t long_ = 26;
    static constexpr size_t signal_ = 9;

    double trailing_stop_;
    double highest_since_entry_;
    bool in_position_;
    quant::ind::MacdResult macd_;
};

int main(int argc, char* argv[]) {
    const auto args = parse_args(argc, argv);

    constexpr double INIT_CASH = 1'000'000.0;
    constexpr double COMMISSION_RATE = 0.0003;
    constexpr int LOT_SIZE = 100;

    fmt::print("MACD 跟踪止损策略回测\n");
    fmt::print("股票：{}({})\n", stock_name(args.stock), args.stock);
    fmt::print("回测区间：{} 至 {}\n", args.start, args.end);
    fmt::print("跟踪止损：{:.2f}%\n", args.trailing_stop * 100.0);
    fmt::print("数据文件：{}\n", args.data_file);

    if (!fs::exists(args.data_file)) {
        fmt::print("错误：数据文件不存在：{}\n", args.data_file);
        return 1;
    }

    auto bars = quant::bt::data::load_from_csv(args.data_file, args.start, args.end);
    if (bars.empty()) {
        fmt::print("错误：回测区间 {} 至 {} 内没有数据\n", args.start, args.end);
        return 1;
    }

    MacdTrailingStopStrategy strategy(args.trailing_stop);
    quant::bt::Backtest bt(INIT_CASH, COMMISSION_RATE, LOT_SIZE);
    auto result = bt.run(bars, strategy);

    quant::bt::print_summary(result, args.stock, stock_name(args.stock), args.start, args.end);

    const std::string prefix = fmt::format("{}/06_macd_trailing_stop", args.output_dir);
    quant::bt::write_result_csv(result, prefix);
    fmt::print("\n已保存净值曲线：{}_nav.csv\n", prefix);
    fmt::print("已保存交易记录：{}_trades.csv\n", prefix);

    return 0;
}
