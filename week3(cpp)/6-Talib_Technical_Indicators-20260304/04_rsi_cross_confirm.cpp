// RSI 交叉确认策略：突破阈值后连续确认 N 根 K 线再交易
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
    int confirm = 2;
};

static CliArgs parse_args(int argc, char* argv[]) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (key == "--confirm") {
            if (i + 1 < argc) args.confirm = std::atoi(argv[++i]);
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

class RsiCrossConfirmStrategy : public quant::bt::Strategy {
public:
    explicit RsiCrossConfirmStrategy(int confirm)
        : confirm_(confirm), buy_counter_(0), sell_counter_(0) {}

    void init(const std::vector<quant::bt::Bar>& bars) override {
        std::vector<double> close;
        close.reserve(bars.size());
        for (const auto& b : bars) close.push_back(b.close);
        rsi_ = quant::ind::rsi(close, period_);
    }

    void next(size_t idx, const std::vector<quant::bt::Bar>& bars, quant::bt::Broker& broker) override {
        if (idx == 0 || idx >= rsi_.size()) return;
        if (!std::isfinite(rsi_[idx - 1]) || !std::isfinite(rsi_[idx])) return;

        const double prev = rsi_[idx - 1];
        const double curr = rsi_[idx];

        // 买入确认逻辑：RSI 上穿 30 后连续在 30 之上
        if (broker.shares() == 0) {
            bool crossed_above = prev <= lower_ && curr > lower_;
            if (crossed_above) {
                buy_counter_ = 1;
            } else if (curr > lower_ && buy_counter_ > 0) {
                ++buy_counter_;
            } else {
                buy_counter_ = 0;
            }

            if (buy_counter_ >= confirm_) {
                broker.buy(idx, bars);
                buy_counter_ = 0;
            }
        }

        // 卖出确认逻辑：RSI 下穿 70 后连续在 70 之下
        if (broker.shares() > 0) {
            bool crossed_below = prev >= upper_ && curr < upper_;
            if (crossed_below) {
                sell_counter_ = 1;
            } else if (curr < upper_ && sell_counter_ > 0) {
                ++sell_counter_;
            } else {
                sell_counter_ = 0;
            }

            if (sell_counter_ >= confirm_) {
                broker.sell(idx, bars);
                sell_counter_ = 0;
            }
        }
    }

private:
    static constexpr size_t period_ = 14;
    static constexpr double lower_ = 30.0;
    static constexpr double upper_ = 70.0;

    int confirm_;
    int buy_counter_;
    int sell_counter_;
    std::vector<double> rsi_;
};

int main(int argc, char* argv[]) {
    const auto args = parse_args(argc, argv);

    constexpr double INIT_CASH = 1'000'000.0;
    constexpr double COMMISSION_RATE = 0.0003;
    constexpr int LOT_SIZE = 100;

    fmt::print("RSI 交叉确认策略回测\n");
    fmt::print("股票：{}({})\n", stock_name(args.stock), args.stock);
    fmt::print("回测区间：{} 至 {}\n", args.start, args.end);
    fmt::print("确认计数：{} 根 K 线\n", args.confirm);
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

    RsiCrossConfirmStrategy strategy(args.confirm);
    quant::bt::Backtest bt(INIT_CASH, COMMISSION_RATE, LOT_SIZE);
    auto result = bt.run(bars, strategy);

    quant::bt::print_summary(result, args.stock, stock_name(args.stock), args.start, args.end);

    const std::string prefix = fmt::format("{}/04_rsi_cross_confirm", args.output_dir);
    quant::bt::write_result_csv(result, prefix);
    fmt::print("\n已保存净值曲线：{}_nav.csv\n", prefix);
    fmt::print("已保存交易记录：{}_trades.csv\n", prefix);

    return 0;
}
