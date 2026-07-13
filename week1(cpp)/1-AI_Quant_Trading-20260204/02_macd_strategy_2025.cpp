// 对应 Python: 2-macd_strategy_2025.py
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
#include "indicators.hpp"

namespace fs = std::filesystem;
using namespace matplot;

struct Trade {
    std::string date;
    std::string action;
    double price;
    int shares;
    double amount;
    double commission;
    double cash;
    double position;
};

int main() {
    const std::string STOCK_CODE = "600519.SH";
    const std::string STOCK_NAME = "贵州茅台";
    const size_t SHORT_PERIOD = 12;
    const size_t LONG_PERIOD = 26;
    const size_t SIGNAL_PERIOD = 9;
    const std::string START_DATE = "2025-01-01";
    const std::string END_DATE = "2025-12-31";
    const double INIT_CASH = 1000000.0;
    const int LOT_SIZE = 100;
    const double COMMISSION_RATE = 0.0003;
    const std::string DATA_FILE = "data/600519_SH_daily.csv";

    if (!fs::exists(DATA_FILE)) {
        fmt::print("错误：数据文件不存在：{}\n", DATA_FILE);
        fmt::print("请先运行数据下载脚本。\n");
        return 1;
    }

    auto csv = quant::csv::read_csv(DATA_FILE);
    if (csv.empty()) {
        fmt::print("错误：无法读取 {}\n", DATA_FILE);
        return 1;
    }

    auto date_col = csv.column("date");
    auto close_col = csv.column_double("close");
    if (date_col.empty() || close_col.empty()) {
        fmt::print("错误：数据缺少 date 或 close 列\n");
        return 1;
    }

    // Parse and sort
    std::vector<std::pair<quant::date::Date, size_t>> indexed;
    for (size_t i = 0; i < date_col.size(); ++i) {
        quant::date::Date d(date_col[i]);
        if (d.valid()) indexed.push_back({d, i});
    }
    std::sort(indexed.begin(), indexed.end());

    std::vector<std::string> all_dates;
    std::vector<double> all_close;
    for (const auto& [d, idx] : indexed) {
        all_dates.push_back(d.to_string());
        all_close.push_back(close_col[idx]);
    }

    quant::date::Date start_d(START_DATE);
    quant::date::Date end_d(END_DATE);

    // Compute MACD on all data
    auto macd = quant::ind::macd(all_close, SHORT_PERIOD, LONG_PERIOD, SIGNAL_PERIOD);

    // Filter backtest period
    std::vector<std::string> dates;
    std::vector<double> close, dif, dea, bar;
    for (size_t i = 0; i < all_dates.size(); ++i) {
        quant::date::Date d(all_dates[i]);
        if (d >= start_d && d <= end_d) {
            dates.push_back(all_dates[i]);
            close.push_back(all_close[i]);
            dif.push_back(macd.dif[i]);
            dea.push_back(macd.dea[i]);
            bar.push_back(macd.bar[i]);
        }
    }

    if (dates.empty()) {
        fmt::print("错误：回测区间 {} 至 {} 内没有数据\n", START_DATE, END_DATE);
        return 1;
    }

    // Backtest
    double cash = INIT_CASH;
    int shares = 0;
    std::vector<Trade> trades;
    std::vector<double> nav(close.size());
    std::vector<double> position(close.size());

    for (size_t i = 0; i < close.size(); ++i) {
        if (i == 0) {
            nav[i] = cash;
            position[i] = 0.0;
            continue;
        }

        double current_price = close[i];
        bool golden_cross = (dif[i - 1] <= dea[i - 1]) && (dif[i] > dea[i]);
        bool death_cross = (dif[i - 1] >= dea[i - 1]) && (dif[i] < dea[i]);

        if (golden_cross && shares == 0) {
            double available_cash = cash / (1.0 + COMMISSION_RATE);
            int max_shares = static_cast<int>(available_cash / current_price / LOT_SIZE) * LOT_SIZE;
            if (max_shares >= LOT_SIZE) {
                double buy_amount = max_shares * current_price;
                double commission = buy_amount * COMMISSION_RATE;
                double total_cost = buy_amount + commission;
                if (total_cost <= cash) {
                    shares = max_shares;
                    cash -= total_cost;
                    double current_nav = cash + shares * current_price;
                    Trade t;
                    t.date = dates[i];
                    t.action = "买入";
                    t.price = current_price;
                    t.shares = shares;
                    t.amount = buy_amount;
                    t.commission = commission;
                    t.cash = cash;
                    t.position = (shares * current_price) / current_nav;
                    trades.push_back(t);
                }
            }
        } else if (death_cross && shares > 0) {
            double sell_amount = shares * current_price;
            double commission = sell_amount * COMMISSION_RATE;
            double net_proceeds = sell_amount - commission;

            Trade t;
            t.date = dates[i];
            t.action = "卖出";
            t.price = current_price;
            t.shares = shares;
            t.amount = sell_amount;
            t.commission = commission;
            t.cash = cash + net_proceeds;
            t.position = 0.0;
            trades.push_back(t);

            cash += net_proceeds;
            shares = 0;
        }

        nav[i] = cash + shares * current_price;
        position[i] = (shares * current_price) / nav[i];
    }

    // Metrics
    double total_return = (nav.back() / INIT_CASH) - 1.0;
    double max_drawdown = 0.0;
    double running_max = nav[0];
    for (double v : nav) {
        running_max = std::max(running_max, v);
        double dd = (v / running_max) - 1.0;
        max_drawdown = std::min(max_drawdown, dd);
    }

    // Print results

    fmt::print("开始回测：{}({}) - MACD策略\n", STOCK_NAME, STOCK_CODE);
    fmt::print("回测区间：{} 至 {}\n", START_DATE, END_DATE);
    fmt::print("初始资金：{:.0f} 元\n", INIT_CASH);
    fmt::print("数据文件：{}\n", DATA_FILE);
    fmt::print("{:-<60}\n", "");
    fmt::print("成功加载 {} 条历史数据\n", all_close.size());
    fmt::print("回测区间内共有 {} 个交易日\n", close.size());
    fmt::print("\n{:=<60}\n", "");
    fmt::print("回测结果\n");
    fmt::print("{:=<60}\n", "");
    fmt::print("股票代码：{} ({})\n", STOCK_CODE, STOCK_NAME);
    fmt::print("回测区间：{} 至 {}\n", START_DATE, END_DATE);
    fmt::print("初始资金：{:.2f} 元\n", INIT_CASH);
    fmt::print("期末净值：{:.2f} 元\n", nav.back());
    fmt::print("总收益率：{:.4f}%\n", total_return * 100.0);
    fmt::print("最大回撤：{:.4f}%\n", max_drawdown * 100.0);
    fmt::print("交易次数：{} 次\n", trades.size());
    fmt::print("期末持仓：{} 股\n", shares);
    fmt::print("期末现金：{:.2f} 元\n", cash);
    fmt::print("{:=<60}\n", "");

    if (!trades.empty()) {
        fmt::print("\n交易记录：\n");
        double total_commission = 0.0;
        for (const auto& t : trades) {
            total_commission += t.commission;
            fmt::print("  {} | {:4s} | 价格: {:.2f} | {}股 | 金额: {:.2f} | 手续费: {:.2f} | 仓位: {:.1f}%\n",
                       t.date, t.action, t.price, t.shares, t.amount, t.commission, t.position * 100.0);
        }
        fmt::print("\n累计手续费：{:.2f} 元\n", total_commission);
    }

    // Save NAV CSV
    fs::create_directories("outputs");
    {
        std::vector<std::string> headers = {"date", "nav", "return", "position"};
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < dates.size(); ++i) {
            rows.push_back({
                dates[i],
                fmt::format("{:.2f}", nav[i]),
                fmt::format("{:.6f}", (nav[i] / INIT_CASH) - 1.0),
                fmt::format("{:.4f}", position[i])
            });
        }
        quant::csv::write_csv("outputs/macd_strategy_2025_nav.csv", headers, rows);
        fmt::print("\n净值曲线已保存至：outputs/macd_strategy_2025_nav.csv\n");
    }

    // Save trades CSV
    if (!trades.empty()) {
        std::vector<std::string> headers = {"date", "action", "price", "shares", "amount", "commission", "cash", "position"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& t : trades) {
            rows.push_back({t.date, t.action,
                            fmt::format("{:.2f}", t.price),
                            fmt::format("{}", t.shares),
                            fmt::format("{:.2f}", t.amount),
                            fmt::format("{:.2f}", t.commission),
                            fmt::format("{:.2f}", t.cash),
                            fmt::format("{:.4f}", t.position)});
        }
        quant::csv::write_csv("outputs/macd_strategy_2025_trades.csv", headers, rows);
        fmt::print("交易记录已保存至：outputs/macd_strategy_2025_trades.csv\n");
    }

    // Save summary
    {
        std::ofstream f("outputs/macd_strategy_2025_summary.txt");
        f << "MACD策略回测报告\n";
        f << "=" << std::string(60, '=') << "\n";
        f << fmt::format("股票代码：{} ({})\n", STOCK_CODE, STOCK_NAME);
        f << fmt::format("回测区间：{} 至 {}\n", START_DATE, END_DATE);
        f << fmt::format("初始资金：{:.2f} 元\n", INIT_CASH);
        f << fmt::format("期末净值：{:.2f} 元\n", nav.back());
        f << fmt::format("总收益率：{:.4f}%\n", total_return * 100.0);
        f << fmt::format("最大回撤：{:.4f}%\n", max_drawdown * 100.0);
        f << fmt::format("交易次数：{} 次\n", trades.size());
        f << fmt::format("期末持仓：{} 股\n", shares);
        f << fmt::format("期末现金：{:.2f} 元\n", cash);
        f.close();
        fmt::print("汇总报告已保存至：outputs/macd_strategy_2025_summary.txt\n");
    }

    // Plot
    auto fig = figure(true);
    fig->size(1400, 1200);
    fig->title(fmt::format("{}({}) MACD策略回测 - 2025年", STOCK_NAME, STOCK_CODE));

    std::vector<double> xs(close.size());
    for (size_t i = 0; i < close.size(); ++i) xs[i] = static_cast<double>(i);

    std::vector<double> buy_x, buy_y, sell_x, sell_y;
    for (const auto& t : trades) {
        for (size_t i = 0; i < dates.size(); ++i) {
            if (dates[i] == t.date) {
                if (t.action == "买入") {
                    buy_x.push_back(xs[i]);
                    buy_y.push_back(close[i]);
                } else {
                    sell_x.push_back(xs[i]);
                    sell_y.push_back(close[i]);
                }
                break;
            }
        }
    }

    auto ax1 = subplot(3, 1, 0);
    ax1->hold(on);
    ax1->plot(xs, close, "b-")->line_width(1.5).display_name("收盘价");
    if (!buy_x.empty()) {
        ax1->scatter(buy_x, buy_y, 12.0)->marker("^").color("red").display_name("买入点");
    }
    if (!sell_x.empty()) {
        ax1->scatter(sell_x, sell_y, 12.0)->marker("v").color("green").display_name("卖出点");
    }
    ax1->ylabel("价格 (元)");
    ax1->title("股价走势与买卖点");
    ax1->grid(on);
    ax1->legend();

    auto ax2 = subplot(3, 1, 1);
    ax2->hold(on);
    ax2->plot(xs, dif, "b-")->line_width(1.2).display_name("DIF");
    ax2->plot(xs, dea)->color("orange").line_width(1.2).display_name("DEA");
    // Use filled rectangles instead of bar() because matplot++ bar() does not
    // render negative y-values correctly in this environment.
    double bar_width = 0.7;
    for (size_t i = 0; i < bar.size(); ++i) {
        double x = xs[i];
        double v = bar[i];
        const char* color = (v >= 0.0) ? "r" : "g";
        std::vector<double> bx = {x - bar_width, x + bar_width, x + bar_width, x - bar_width};
        std::vector<double> by = {0.0, 0.0, v, v};
        ax2->fill(bx, by, color)->marker_face_color("none").marker_color("none");
    }
    ax2->plot(xs, std::vector<double>(xs.size(), 0.0))->color("black").line_style("--").line_width(1.5);
    ax2->ylabel("MACD");
    ax2->title(fmt::format("MACD指标 (快线={}, 慢线={}, 信号线={})", SHORT_PERIOD, LONG_PERIOD, SIGNAL_PERIOD));
    ax2->grid(on);
    ax2->legend();

    auto ax3 = subplot(3, 1, 2);
    ax3->hold(on);
    std::vector<double> nav_wan(nav.size());
    for (size_t i = 0; i < nav.size(); ++i) nav_wan[i] = nav[i] / 10000.0;
    ax3->plot(xs, nav_wan)->color(matplot::color::magenta).line_width(1.5).display_name("资金曲线");
    ax3->plot(xs, std::vector<double>(xs.size(), INIT_CASH / 10000.0))->color("black").line_style("--").display_name("初始资金");
    ax3->ylabel("资金 (万元)");
    ax3->xlabel("日期");
    ax3->title("资金曲线");
    ax3->grid(on);
    ax3->legend();

    try {
        fig->save("outputs/macd_strategy_2025_chart.png");
        fmt::print("\n策略图表已保存至：outputs/macd_strategy_2025_chart.png\n");
    } catch (const std::exception& e) {
        fmt::print("\n保存图表失败：{}\n", e.what());
        return 1;
    }

    return 0;
}
