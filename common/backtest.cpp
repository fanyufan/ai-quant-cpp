#include "backtest.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <fmt/format.h>

#include "csv.hpp"

namespace quant::bt {

namespace fs = std::filesystem;

Broker::Broker(double cash, double commission_rate, int lot_size)
    : cash_(cash), shares_(0), commission_rate_(commission_rate), lot_size_(lot_size) {}

double Broker::position_ratio(double price) const {
    double n = nav(price);
    if (n <= 0.0) return 0.0;
    return (shares_ * price) / n;
}

void Broker::buy(size_t idx, const std::vector<Bar>& bars, double target_cash) {
    if (idx >= bars.size()) return;
    Trade t;
    t.date = bars[idx].date;
    t.action = "BUY";
    t.price = bars[idx].close;

    double usable_cash = (target_cash < 0.0) ? cash_ : std::min(target_cash, cash_);
    double cash_after_commission = usable_cash / (1.0 + commission_rate_);
    int max_shares = static_cast<int>(cash_after_commission / t.price / lot_size_) * lot_size_;

    if (max_shares < lot_size_) {
        return;
    }

    t.shares = max_shares;
    t.amount = t.shares * t.price;
    t.commission = t.amount * commission_rate_;
    double total_cost = t.amount + t.commission;

    if (total_cost <= cash_ + 1e-6) {
        cash_ -= total_cost;
        shares_ += t.shares;
        t.cash = cash_;
        t.position_ratio = position_ratio(t.price);
        trades_.push_back(t);
    }
}

void Broker::sell(size_t idx, const std::vector<Bar>& bars, int target_shares) {
    if (idx >= bars.size()) return;
    Trade t;
    t.date = bars[idx].date;
    t.action = "SELL";
    t.price = bars[idx].close;

    int shares_to_sell = (target_shares < 0) ? shares_ : std::min(target_shares, shares_);
    if (shares_to_sell <= 0) {
        return;
    }

    t.shares = shares_to_sell;
    t.amount = t.shares * t.price;
    t.commission = t.amount * commission_rate_;
    double net_proceeds = t.amount - t.commission;

    cash_ += net_proceeds;
    shares_ -= t.shares;

    t.cash = cash_;
    t.position_ratio = position_ratio(t.price);
    trades_.push_back(t);
}

Backtest::Backtest(double init_cash, double commission_rate, int lot_size)
    : init_cash_(init_cash), commission_rate_(commission_rate), lot_size_(lot_size) {}

Result Backtest::run(const std::vector<Bar>& bars, Strategy& strategy) {
    Result result;
    result.init_cash = init_cash_;

    Broker broker(init_cash_, commission_rate_, lot_size_);
    strategy.init(bars);

    double running_max = init_cash_;

    for (size_t i = 0; i < bars.size(); ++i) {
        if (i > 0) {
            strategy.next(i, bars, broker);
        }

        double price = bars[i].close;
        double nav = broker.nav(price);
        running_max = std::max(running_max, nav);
        double dd = (nav / running_max) - 1.0;

        DailyRecord rec;
        rec.date = bars[i].date;
        rec.nav = nav;
        rec.cash = broker.cash();
        rec.shares = broker.shares();
        rec.price = price;
        rec.position_ratio = broker.position_ratio(price);
        rec.total_return = (nav / init_cash_) - 1.0;
        result.records.push_back(rec);

        if (result.max_drawdown > dd) {
            result.max_drawdown = dd;
        }
    }

    result.trades = broker.trades();
    result.total_trades = result.trades.size();
    result.final_nav = result.records.empty() ? init_cash_ : result.records.back().nav;
    result.total_return = (result.final_nav / init_cash_) - 1.0;
    return result;
}

void write_result_csv(const Result& result, const std::string& prefix) {
    fs::create_directories(fs::path(prefix).parent_path());

    // 净值曲线
    {
        std::vector<std::string> headers = {"date", "nav", "return", "position"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& r : result.records) {
            rows.push_back({
                r.date,
                fmt::format("{:.2f}", r.nav),
                fmt::format("{:.6f}", r.total_return),
                fmt::format("{:.4f}", r.position_ratio)
            });
        }
        quant::csv::write_csv(prefix + "_nav.csv", headers, rows);
    }

    // 交易记录
    if (!result.trades.empty()) {
        std::vector<std::string> headers = {"date", "action", "price", "shares", "amount", "commission", "cash", "position"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& t : result.trades) {
            rows.push_back({
                t.date,
                t.action,
                fmt::format("{:.2f}", t.price),
                fmt::format("{}", t.shares),
                fmt::format("{:.2f}", t.amount),
                fmt::format("{:.2f}", t.commission),
                fmt::format("{:.2f}", t.cash),
                fmt::format("{:.4f}", t.position_ratio)
            });
        }
        quant::csv::write_csv(prefix + "_trades.csv", headers, rows);
    }
}

void print_summary(const Result& result,
                   const std::string& stock_code,
                   const std::string& stock_name,
                   const std::string& start_date,
                   const std::string& end_date) {
    fmt::print("开始回测：{}({})\n", stock_name, stock_code);
    fmt::print("回测区间：{} 至 {}\n", start_date, end_date);
    fmt::print("初始资金：{:.0f} 元\n", result.init_cash);
    fmt::print("数据条数：{}\n", result.records.size());
    fmt::print("{:-<60}\n", "");
    fmt::print("回测结果\n");
    fmt::print("{:=<60}\n", "");
    fmt::print("股票代码：{} ({})", stock_code, stock_name);
    fmt::print("\n");
    fmt::print("回测区间：{} 至 {}\n", start_date, end_date);
    fmt::print("初始资金：{:.2f} 元\n", result.init_cash);
    fmt::print("期末净值：{:.2f} 元\n", result.final_nav);
    fmt::print("总收益率：{:.4f}%\n", result.total_return * 100.0);
    fmt::print("最大回撤：{:.4f}%\n", result.max_drawdown * 100.0);
    fmt::print("交易次数：{} 次\n", result.total_trades);
    if (!result.records.empty()) {
        fmt::print("期末持仓：{} 股\n", result.records.back().shares);
        fmt::print("期末现金：{:.2f} 元\n", result.records.back().cash);
    }
    fmt::print("{:=<60}\n", "");

    if (!result.trades.empty()) {
        fmt::print("\n交易记录：\n");
        double total_commission = 0.0;
        for (const auto& t : result.trades) {
            total_commission += t.commission;
            std::string action_cn = (t.action == "BUY") ? "买入" : "卖出";
            fmt::print("  {} | {:4s} | 价格: {:.2f} | {}股 | 金额: {:.2f} | 手续费: {:.2f} | 仓位: {:.1f}%\n",
                       t.date, action_cn, t.price, t.shares, t.amount, t.commission, t.position_ratio * 100.0);
        }
        fmt::print("\n累计手续费：{:.2f} 元\n", total_commission);
    }
}

} // namespace quant::bt
