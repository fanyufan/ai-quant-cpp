// 对应 Python: 3-grid_strategy_2025.py
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <matplot/matplot.h>
#include "csv.hpp"
#include "date_utils.hpp"

namespace fs = std::filesystem;
using namespace matplot;

struct Trade {
    std::string date;
    std::string action;
    double grid_price;
    double exec_price;
    int shares;
    double amount;
    double commission;
    double cash;
    int total_shares;
    int position_level;
    double nav;
};

struct GridStrategy {
    double center_price;
    int grid_shares;
    std::vector<double> buy_grid;
    std::vector<double> sell_grid;
    double commission_rate;
    double cash;
    int shares;
    int position_level;
    std::vector<Trade> trades;

    GridStrategy(double center, int shares_per_trade,
                 const std::vector<double>& buy_prices,
                 const std::vector<double>& sell_prices,
                 double init_cash, int init_shares, double rate)
        : center_price(center), grid_shares(shares_per_trade),
          buy_grid(buy_prices), sell_grid(sell_prices),
          commission_rate(rate), cash(init_cash), shares(init_shares) {
        std::sort(buy_grid.begin(), buy_grid.end(), std::greater<double>());
        std::sort(sell_grid.begin(), sell_grid.end());
        position_level = (grid_shares > 0) ? (init_shares / grid_shares) : 0;
    }

    double nav(double current_price) const {
        return cash + shares * current_price;
    }

    Trade* execute(const std::string& date, double current_price, double prev_price) {
        if (current_price <= 0 || prev_price <= 0) return nullptr;

        // Buy check
        if (position_level < static_cast<int>(buy_grid.size())) {
            double target_buy = buy_grid[position_level];
            if (prev_price > target_buy && current_price <= target_buy) {
                double buy_amount = grid_shares * target_buy;
                double commission = buy_amount * commission_rate;
                double total_cost = buy_amount + commission;
                if (cash >= total_cost) {
                    cash -= total_cost;
                    shares += grid_shares;
                    position_level++;
                    Trade t;
                    t.date = date;
                    t.action = "买入";
                    t.grid_price = target_buy;
                    t.exec_price = target_buy;
                    t.shares = grid_shares;
                    t.amount = buy_amount;
                    t.commission = commission;
                    t.cash = cash;
                    t.total_shares = shares;
                    t.position_level = position_level;
                    t.nav = nav(current_price);
                    trades.push_back(t);
                    return &trades.back();
                }
            }
        }

        // Sell check
        if (position_level > 0) {
            int sell_index = position_level - 1;
            if (sell_index < static_cast<int>(sell_grid.size())) {
                double target_sell = sell_grid[sell_index];
                if (prev_price < target_sell && current_price >= target_sell) {
                    if (shares >= grid_shares) {
                        double sell_amount = grid_shares * target_sell;
                        double commission = sell_amount * commission_rate;
                        double net = sell_amount - commission;
                        cash += net;
                        shares -= grid_shares;
                        position_level--;
                        Trade t;
                        t.date = date;
                        t.action = "卖出";
                        t.grid_price = target_sell;
                        t.exec_price = target_sell;
                        t.shares = grid_shares;
                        t.amount = sell_amount;
                        t.commission = commission;
                        t.cash = cash;
                        t.total_shares = shares;
                        t.position_level = position_level;
                        t.nav = nav(current_price);
                        trades.push_back(t);
                        return &trades.back();
                    }
                }
            }
        }
        return nullptr;
    }
};

int main() {
    const std::string STOCK_CODE = "600519.SH";
    const std::string STOCK_NAME = "贵州茅台";
    const double CENTER_PRICE = 1500.0;
    const int GRID_SHARES = 100;
    const std::vector<double> BUY_GRID_PRICES = {1450, 1400, 1350, 1300};
    const std::vector<double> SELL_GRID_PRICES = {1550, 1600, 1650, 1700};
    const double INIT_CASH = 1000000.0;
    const int INIT_SHARES = 0;
    const std::string START_DATE = "2025-01-01";
    const std::string END_DATE = "2025-12-31";
    const double COMMISSION_RATE = 0.0003;
    const std::string DATA_FILE = "data/600519_SH_daily.csv";

    fmt::print("{:=<70}\n", "");
    fmt::print("网格交易策略回测\n");
    fmt::print("{:=<70}\n", "");
    fmt::print("股票：{}({})\n", STOCK_NAME, STOCK_CODE);
    fmt::print("回测区间：{} 至 {}\n", START_DATE, END_DATE);
    fmt::print("数据文件：{}\n", DATA_FILE);
    fmt::print("{:-<70}\n", "");

    if (!fs::exists(DATA_FILE)) {
        fmt::print("错误：数据文件不存在：{}\n", DATA_FILE);
        return 1;
    }

    auto csv = quant::csv::read_csv(DATA_FILE);
    auto date_col = csv.column("date");
    auto close_col = csv.column_double("close");
    if (date_col.empty() || close_col.empty()) {
        fmt::print("错误：数据缺少 date 或 close 列\n");
        return 1;
    }

    std::vector<std::pair<quant::date::Date, double>> data;
    for (size_t i = 0; i < date_col.size(); ++i) {
        quant::date::Date d(date_col[i]);
        if (d.valid()) data.push_back({d, close_col[i]});
    }
    std::sort(data.begin(), data.end());

    quant::date::Date start_d(START_DATE);
    quant::date::Date end_d(END_DATE);

    std::vector<std::string> dates;
    std::vector<double> close;
    for (const auto& [d, c] : data) {
        if (d >= start_d && d <= end_d) {
            dates.push_back(d.to_string());
            close.push_back(c);
        }
    }

    if (dates.empty()) {
        fmt::print("错误：回测区间 {} 至 {} 内没有数据\n", START_DATE, END_DATE);
        return 1;
    }

    GridStrategy strategy(CENTER_PRICE, GRID_SHARES, BUY_GRID_PRICES, SELL_GRID_PRICES,
                          INIT_CASH, INIT_SHARES, COMMISSION_RATE);

    fmt::print("\n网格策略初始化完成：\n");
    fmt::print("  中心价格：{}\n", CENTER_PRICE);
    fmt::print("  每次交易：{}股\n", GRID_SHARES);
    fmt::print("  买入网格：");
    for (double p : strategy.buy_grid) fmt::print("{} ", p);
    fmt::print("（从高到低）\n");
    fmt::print("  卖出网格：");
    for (double p : strategy.sell_grid) fmt::print("{} ", p);
    fmt::print("（从低到高）\n");
    fmt::print("  初始现金：{:.2f}元\n", INIT_CASH);
    fmt::print("  初始持股：{}股\n", INIT_SHARES);
    fmt::print("  初始层级：{}\n", strategy.position_level);

    std::vector<double> nav(close.size());
    nav[0] = strategy.nav(close[0]);

    for (size_t i = 1; i < close.size(); ++i) {
        strategy.execute(dates[i], close[i], close[i - 1]);
        nav[i] = strategy.nav(close[i]);
    }

    const auto& trades = strategy.trades;
    size_t buy_count = 0, sell_count = 0;
    double total_commission = 0.0;
    double total_buy = 0.0, total_sell = 0.0;
    for (const auto& t : trades) {
        total_commission += t.commission;
        if (t.action == "买入") { buy_count++; total_buy += t.exec_price; }
        else { sell_count++; total_sell += t.exec_price; }
    }
    double avg_buy = buy_count ? (total_buy / buy_count) : 0.0;
    double avg_sell = sell_count ? (total_sell / sell_count) : 0.0;

    double total_return = (nav.back() / nav.front()) - 1.0;
    double max_drawdown = 0.0;
    double running_max = nav[0];
    for (double v : nav) {
        running_max = std::max(running_max, v);
        max_drawdown = std::min(max_drawdown, (v / running_max) - 1.0);
    }

    fmt::print("\n{:=<70}\n", "");
    fmt::print("回测结果\n");
    fmt::print("{:=<70}\n", "");
    fmt::print("股票代码：{} ({})\n", STOCK_CODE, STOCK_NAME);
    fmt::print("回测区间：{} 至 {}\n", START_DATE, END_DATE);
    fmt::print("网格参数：中心{}，每次{}股\n", CENTER_PRICE, GRID_SHARES);
    fmt::print("买入网格：");
    for (double p : BUY_GRID_PRICES) fmt::print("{} ", p);
    fmt::print("\n卖出网格：");
    for (double p : SELL_GRID_PRICES) fmt::print("{} ", p);
    fmt::print("\n{:-<70}\n", "");
    fmt::print("初始资金：{:.2f} 元\n", nav.front());
    fmt::print("期末资金：{:.2f} 元\n", nav.back());
    fmt::print("总收益率：{:.4%}\n", total_return);
    fmt::print("最大回撤：{:.4%}\n", max_drawdown);
    fmt::print("{:-<70}\n", "");
    fmt::print("总交易次数：{} 次\n", trades.size());
    fmt::print("  买入次数：{} 次\n", buy_count);
    fmt::print("  卖出次数：{} 次\n", sell_count);
    fmt::print("累计手续费：{:.2f} 元\n", total_commission);
    if (avg_buy > 0) fmt::print("平均买入价：{:.2f} 元\n", avg_buy);
    if (avg_sell > 0) fmt::print("平均卖出价：{:.2f} 元\n", avg_sell);
    fmt::print("{:-<70}\n", "");
    fmt::print("期末持股：{} 股\n", strategy.shares);
    fmt::print("期末现金：{:.2f} 元\n", strategy.cash);
    fmt::print("{:=<70}\n", "");

    if (!trades.empty()) {
        fmt::print("\n交易记录：\n");
        fmt::print("{:-<100}\n", "");
        fmt::print("{:<12} {:<6} {:<10} {:<10} {:<8} {:<14} {:<10} {:<8} {:<6} {:<14}\n",
                   "日期", "操作", "网格价", "成交价", "股数", "金额", "手续费", "持股", "层级", "净值");
        fmt::print("{:-<100}\n", "");
        for (const auto& t : trades) {
            fmt::print("{:<12} {:<6} {:<10.2f} {:<10.2f} {:<8} {:<14.2f} {:<10.2f} {:<8} {:<6} {:<14.2f}\n",
                       t.date, t.action, t.grid_price, t.exec_price, t.shares,
                       t.amount, t.commission, t.total_shares, t.position_level, t.nav);
        }
        fmt::print("{:-<100}\n", "");
    } else {
        fmt::print("\n交易记录：无交易（价格未触及任何网格线）\n");
    }

    fs::create_directories("outputs");
    {
        std::vector<std::string> headers = {"date", "nav", "return"};
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < dates.size(); ++i) {
            rows.push_back({dates[i], fmt::format("{:.2f}", nav[i]),
                            fmt::format("{:.6f}", (nav[i] / nav.front()) - 1.0)});
        }
        quant::csv::write_csv("outputs/grid_strategy_2025_nav.csv", headers, rows);
        fmt::print("\n净值曲线已保存至：outputs/grid_strategy_2025_nav.csv\n");
    }

    if (!trades.empty()) {
        std::vector<std::string> headers = {"date", "action", "grid_price", "exec_price", "shares",
                                            "amount", "commission", "cash", "total_shares",
                                            "position_level", "nav"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& t : trades) {
            rows.push_back({t.date, t.action,
                            fmt::format("{:.2f}", t.grid_price),
                            fmt::format("{:.2f}", t.exec_price),
                            fmt::format("{}", t.shares),
                            fmt::format("{:.2f}", t.amount),
                            fmt::format("{:.2f}", t.commission),
                            fmt::format("{:.2f}", t.cash),
                            fmt::format("{}", t.total_shares),
                            fmt::format("{}", t.position_level),
                            fmt::format("{:.2f}", t.nav)});
        }
        quant::csv::write_csv("outputs/grid_strategy_2025_trades.csv", headers, rows);
        fmt::print("交易记录已保存至：outputs/grid_strategy_2025_trades.csv\n");
    }

    {
        std::ofstream f("outputs/grid_strategy_2025_summary.txt");
        f << "网格交易策略回测报告\n";
        f << "=" << std::string(60, '=') << "\n";
        f << fmt::format("股票代码：{} ({})\n", STOCK_CODE, STOCK_NAME);
        f << fmt::format("回测区间：{} 至 {}\n", START_DATE, END_DATE);
        f << fmt::format("网格参数：中心{}，每次{}股\n", CENTER_PRICE, GRID_SHARES);
        f << "买入网格：";
        for (double p : BUY_GRID_PRICES) f << p << " ";
        f << "\n卖出网格：";
        for (double p : SELL_GRID_PRICES) f << p << " ";
        f << "\n" << std::string(60, '-') << "\n";
        f << fmt::format("初始资金：{:.2f} 元\n", nav.front());
        f << fmt::format("期末资金：{:.2f} 元\n", nav.back());
        f << fmt::format("总收益率：{:.4%}\n", total_return);
        f << fmt::format("最大回撤：{:.4%}\n", max_drawdown);
        f << std::string(60, '-') << "\n";
        f << fmt::format("总交易次数：{} 次\n", trades.size());
        f << fmt::format("  买入次数：{} 次\n", buy_count);
        f << fmt::format("  卖出次数：{} 次\n", sell_count);
        f << fmt::format("累计手续费：{:.2f} 元\n", total_commission);
        f << std::string(60, '-') << "\n";
        f << fmt::format("期末持股：{} 股\n", strategy.shares);
        f << fmt::format("期末现金：{:.2f} 元\n", strategy.cash);
        f.close();
        fmt::print("汇总报告已保存至：outputs/grid_strategy_2025_summary.txt\n");
    }

    // Plot
    auto fig = figure(true);
    fig->size(1400, 1000);

    std::vector<double> xs(close.size());
    for (size_t i = 0; i < close.size(); ++i) xs[i] = static_cast<double>(i);

    std::vector<double> buy_x, buy_y, sell_x, sell_y;
    for (const auto& t : trades) {
        for (size_t i = 0; i < dates.size(); ++i) {
            if (dates[i] == t.date) {
                if (t.action == "买入") { buy_x.push_back(xs[i]); buy_y.push_back(t.exec_price); }
                else { sell_x.push_back(xs[i]); sell_y.push_back(t.exec_price); }
                break;
            }
        }
    }

    auto ax1 = subplot(2, 1, 0);
    ax1->plot(xs, close, "b-")->line_width(1.5).display_name("收盘价");
    ax1->plot(xs, std::vector<double>(xs.size(), CENTER_PRICE), "gray-")->line_width(2.0).display_name(fmt::format("中心线 {}", CENTER_PRICE));
    for (double p : BUY_GRID_PRICES) {
        ax1->plot(xs, std::vector<double>(xs.size(), p), "g--")->line_width(1.0).display_name(fmt::format("买入 {}", static_cast<int>(p)));
    }
    for (double p : SELL_GRID_PRICES) {
        ax1->plot(xs, std::vector<double>(xs.size(), p), "r--")->line_width(1.0).display_name(fmt::format("卖出 {}", static_cast<int>(p)));
    }
    if (!buy_x.empty()) ax1->scatter(buy_x, buy_y, 12.0)->marker("^").color("green").display_name("买入点");
    if (!sell_x.empty()) ax1->scatter(sell_x, sell_y, 12.0)->marker("v").color("red").display_name("卖出点");
    ax1->ylabel("股价 (元)");
    ax1->title(fmt::format("{}({}) 网格交易策略 - 2025年", STOCK_NAME, STOCK_CODE));
    ax1->grid(on);
    ax1->legend()->location(legend::general_alignment::topright);

    auto ax2 = subplot(2, 1, 1);
    std::vector<double> nav_wan(nav.size());
    for (size_t i = 0; i < nav.size(); ++i) nav_wan[i] = nav[i] / 10000.0;
    ax2->plot(xs, nav_wan, "purple")->line_width(1.5).display_name("资金曲线");
    ax2->plot(xs, std::vector<double>(xs.size(), INIT_CASH / 10000.0), "k--")->display_name("初始资金");
    ax2->ylabel("资金 (万元)");
    ax2->xlabel("日期");
    ax2->title("资金曲线");
    ax2->grid(on);
    ax2->legend()->location(legend::general_alignment::topleft);

    fig->save("outputs/grid_strategy_2025_chart.png");
    fmt::print("\n策略图表已保存至：outputs/grid_strategy_2025_chart.png\n");

    fmt::print("\n{:=<70}\n", "");
    fmt::print("回测完成!\n");
    fmt::print("{:=<70}\n", "");

    return 0;
}
