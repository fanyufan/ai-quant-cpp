// 3-实盘交易绩效分析
// 对应 week7/13-QuantStats绩效分析与报告-20260328/3-实盘交易绩效分析.py
// C++ 中简化 QuantStats/HTML 报告为控制台 + 文本文件 + Matplot++ 图表输出。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <matplot/matplot.h>

#include "backtest_data_mysql.hpp"
#include "csv.hpp"
#include "env.hpp"

using namespace quant;

namespace {

const double NaN = std::numeric_limits<double>::quiet_NaN();

struct Trade {
    std::string date;
    std::string code;
    std::string name;
    int side = 0; // 1 buy, -1 sell
    long long volume = 0;
    double price = 0.0;
    double amount = 0.0; // 成交金额
    double fee = 0.0;
};

struct StockResult {
    std::string code, name;
    int buy_count = 0, sell_count = 0;
    long long buy_volume = 0, sell_volume = 0;
    double buy_amount = 0.0, sell_amount = 0.0;
    double cost_basis = 0.0;
    double realized_pnl = 0.0;
    double remaining_qty = 0.0;
};

struct DailyRecord {
    std::string date;
    double cash = 0.0;
    double market_value = 0.0;
    double nav = 0.0;
    double daily_pnl = 0.0;
    double cum_pnl = 0.0;
};

struct Metrics {
    double total_return = 0.0;
    double annual_return = 0.0;
    double sharpe = 0.0;
    double sortino = 0.0;
    double volatility = 0.0;
    double downside_dev = 0.0;
    double max_dd = 0.0;
    double max_dd_duration = 0.0;
    double calmar = 0.0;
    double win_rate = 0.0;
    double profit_factor = 0.0;
    double avg_daily_pnl = 0.0;
    size_t total_trades = 0;
};

double vec_sum(const std::vector<double>& v) {
    return std::accumulate(v.begin(), v.end(), 0.0);
}

double vec_mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return vec_sum(v) / static_cast<double>(v.size());
}

std::string code_to_standard(const std::string& raw) {
    std::string digits;
    for (char c : raw) {
        if (c >= '0' && c <= '9') digits.push_back(c);
    }
    if (digits.size() != 6) return "";
    char first = digits[0];
    if (first == '0' || first == '3' || first == '6') {
        if (first == '0' || first == '3') return digits + ".SZ";
        return digits + ".SH";
    }
    return "";
}

bool is_a_share(const std::string& code) {
    if (code.size() != 9) return false;
    char first = code[0];
    if (first != '0' && first != '3' && first != '6') return false;
    std::string suffix = code.substr(7, 2);
    return suffix == "SH" || suffix == "SZ";
}

std::optional<size_t> find_col(const quant::csv::CsvData& data,
                               const std::vector<std::string>& aliases) {
    for (const auto& a : aliases) {
        auto idx = data.col_index(a);
        if (idx) return idx;
    }
    return std::nullopt;
}

std::vector<Trade> parse_csv(const std::string& path) {
    std::vector<Trade> trades;
    auto data = quant::csv::read_csv(path);
    if (data.empty()) {
        fmt::print("  [警告] 无法读取: {}\n", path);
        return trades;
    }

    auto date_col = find_col(data, {"成交日期", "成交时间", "日期", "date", "trade_date"});
    auto code_col = find_col(data, {"证券代码", "代码", "code", "stock_code"});
    auto name_col = find_col(data, {"证券名称", "名称", "name", "stock_name"});
    auto op_col = find_col(data, {"操作", "方向", "side", "type", "买卖", "成交方向"});
    auto vol_col = find_col(data, {"成交数量", "数量", "volume", "quantity"});
    auto price_col = find_col(data, {"成交均价", "价格", "price", "成交价格"});
    auto amount_col = find_col(data, {"成交金额", "金额", "amount", "成交额"});
    auto fee_col = find_col(data, {"手续费", "佣金", "fee", "commission"});

    if (!date_col || !code_col || !op_col || !vol_col || !price_col) {
        fmt::print("  [警告] {} 缺少必要列, 已跳过\n", path);
        return trades;
    }

    auto to_int = [](const std::string& s) -> long long {
        try { return std::stoll(s); } catch (...) { return 0; }
    };
    auto to_double = [](const std::string& s) -> double {
        try { return std::stod(s); } catch (...) { return 0.0; }
    };

    for (size_t r = 0; r < data.rows.size(); ++r) {
        const auto& row = data.rows[r];
        std::string raw_code = row[*code_col];
        std::string std_code = code_to_standard(raw_code);
        Trade t;
        t.date = row[*date_col];
        if (t.date.size() == 8 && std::all_of(t.date.begin(), t.date.end(), ::isdigit)) {
            t.date = t.date.substr(0, 4) + "-" + t.date.substr(4, 2) + "-" + t.date.substr(6, 2);
        }
        t.code = std_code;
        if (name_col) t.name = row[*name_col];

        std::string op = op_col ? row[*op_col] : "";
        std::transform(op.begin(), op.end(), op.begin(), ::tolower);
        if (op.find("buy") != std::string::npos || op.find("买入") != std::string::npos ||
            op.find("买") != std::string::npos) {
            t.side = 1;
        } else if (op.find("sell") != std::string::npos || op.find("卖出") != std::string::npos ||
                   op.find("卖") != std::string::npos) {
            t.side = -1;
        } else {
            continue;
        }

        t.volume = std::llabs(to_int(row[*vol_col]));
        t.price = std::abs(to_double(row[*price_col]));
        if (amount_col) {
            t.amount = std::abs(to_double(row[*amount_col]));
        } else {
            t.amount = t.price * static_cast<double>(t.volume);
        }
        if (fee_col) t.fee = std::abs(to_double(row[*fee_col]));

        if (t.volume == 0 || t.price <= 0.0) continue;
        trades.push_back(t);
    }
    return trades;
}

std::vector<Trade> load_broker_csvs(const std::vector<std::string>& paths) {
    std::vector<Trade> all;
    for (const auto& p : paths) {
        auto t = parse_csv(p);
        all.insert(all.end(), t.begin(), t.end());
    }
    std::sort(all.begin(), all.end(), [](const Trade& a, const Trade& b) {
        if (a.date != b.date) return a.date < b.date;
        if (a.code != b.code) return a.code < b.code;
        if (a.side != b.side) return a.side < b.side;
        if (a.volume != b.volume) return a.volume < b.volume;
        return a.price < b.price;
    });
    all.erase(std::unique(all.begin(), all.end(), [](const Trade& a, const Trade& b) {
        return a.date == b.date && a.code == b.code && a.side == b.side &&
               a.volume == b.volume && std::abs(a.price - b.price) < 1e-6;
    }), all.end());
    return all;
}

std::map<std::string, StockResult> analyze_by_stock(const std::vector<Trade>& trades) {
    std::map<std::string, StockResult> out;
    for (const auto& t : trades) {
        auto& sr = out[t.code];
        sr.code = t.code;
        sr.name = t.name;
        if (t.side == 1) {
            sr.buy_count++;
            sr.buy_volume += t.volume;
            sr.buy_amount += t.amount;
            double total_cost = sr.cost_basis * sr.remaining_qty + t.amount + t.fee;
            sr.remaining_qty += static_cast<double>(t.volume);
            sr.cost_basis = sr.remaining_qty > 0.0 ? total_cost / sr.remaining_qty : 0.0;
        } else {
            sr.sell_count++;
            sr.sell_volume += t.volume;
            sr.sell_amount += t.amount;
            sr.realized_pnl += (t.price - sr.cost_basis) * static_cast<double>(t.volume) - t.fee;
            sr.remaining_qty -= static_cast<double>(t.volume);
            if (sr.remaining_qty <= 1e-9) {
                sr.remaining_qty = 0.0;
                sr.cost_basis = 0.0;
            }
        }
    }
    return out;
}

std::vector<DailyRecord> build_portfolio_nav(
    const std::vector<Trade>& trades,
    const quant::mysql::Config& cfg,
    double initial_cash,
    std::map<std::string, double>& final_positions,
    std::map<std::string, double>& final_cost_basis) {

    std::vector<DailyRecord> records;
    std::set<std::string> all_dates;
    std::set<std::string> traded_codes;
    for (const auto& t : trades) {
        all_dates.insert(t.date);
        traded_codes.insert(t.code);
    }
    if (all_dates.empty()) return records;

    std::map<std::string, std::map<std::string, double>> close_maps;
    for (const auto& code : traded_codes) {
        auto bars = bt::data::load_from_mysql(cfg, code, *all_dates.begin(), *all_dates.rbegin());
        auto& mp = close_maps[code];
        for (const auto& b : bars) mp[b.date] = b.close;
    }

    for (const auto& kv : close_maps) {
        for (const auto& d : kv.second) all_dates.insert(d.first);
    }
    std::vector<std::string> dates(all_dates.begin(), all_dates.end());

    std::map<std::string, double> positions;
    std::map<std::string, double> cost_basis;
    double cash = initial_cash;

    std::map<std::string, std::vector<Trade>> trades_by_date;
    for (const auto& t : trades) trades_by_date[t.date].push_back(t);

    double prev_nav = initial_cash;
    for (const auto& d : dates) {
        auto it = trades_by_date.find(d);
        if (it != trades_by_date.end()) {
            for (const auto& t : it->second) {
                if (t.side == 1) {
                    double flow = t.amount + t.fee;
                    if (cash >= flow - 1e-6) {
                        cash -= flow;
                        double total_cost = cost_basis[t.code] * positions[t.code] + t.amount + t.fee;
                        positions[t.code] += static_cast<double>(t.volume);
                        cost_basis[t.code] = positions[t.code] > 0.0
                                                 ? total_cost / positions[t.code]
                                                 : 0.0;
                    }
                } else {
                    cash += (t.amount - t.fee);
                    positions[t.code] -= static_cast<double>(t.volume);
                    if (positions[t.code] <= 1e-9) {
                        positions[t.code] = 0.0;
                        cost_basis[t.code] = 0.0;
                    }
                }
            }
        }

        double market_value = 0.0;
        for (const auto& kv : positions) {
            if (kv.second <= 1e-9) continue;
            const auto& mp = close_maps[kv.first];
            auto pit = mp.find(d);
            double price = 0.0;
            if (pit != mp.end()) price = pit->second;
            else {
                for (auto it2 = mp.rbegin(); it2 != mp.rend(); ++it2) {
                    if (it2->first <= d) { price = it2->second; break; }
                }
            }
            market_value += kv.second * price;
        }
        double nav = cash + market_value;
        DailyRecord rec;
        rec.date = d;
        rec.cash = cash;
        rec.market_value = market_value;
        rec.nav = nav;
        rec.daily_pnl = nav - prev_nav;
        rec.cum_pnl = nav - initial_cash;
        records.push_back(rec);
        prev_nav = nav;
    }
    final_positions = positions;
    final_cost_basis = cost_basis;
    return records;
}

Metrics calculate_metrics(const std::vector<DailyRecord>& recs, double initial_cash) {
    Metrics m;
    if (recs.empty()) return m;
    size_t n = recs.size();
    double start = initial_cash;
    double end = recs.back().nav;
    m.total_return = (end - start) / start;
    m.annual_return = std::pow(1.0 + m.total_return, 252.0 / static_cast<double>(n)) - 1.0;

    std::vector<double> rets;
    rets.reserve(n - 1);
    double win_sum = 0.0, loss_sum = 0.0;
    int wins = 0;
    for (size_t i = 1; i < n; ++i) {
        double r = (recs[i].nav - recs[i - 1].nav) / recs[i - 1].nav;
        rets.push_back(r);
        if (r > 0.0) { win_sum += r; wins++; }
        else loss_sum += -r;
    }
    m.avg_daily_pnl = vec_mean(std::vector<double>(rets.begin(), rets.end())) * initial_cash;

    double mean_r = vec_mean(rets);
    double var = 0.0, dvar = 0.0;
    for (double r : rets) {
        double d = r - mean_r;
        var += d * d;
        if (r < 0.0) dvar += r * r;
    }
    var = rets.empty() ? 0.0 : var / rets.size();
    m.volatility = std::sqrt(var) * std::sqrt(252.0);
    m.downside_dev = rets.empty() ? 0.0 : std::sqrt(dvar / rets.size()) * std::sqrt(252.0);
    m.sharpe = m.volatility > 0.0 ? (mean_r * 252.0) / m.volatility : 0.0;
    m.sortino = m.downside_dev > 0.0 ? (mean_r * 252.0) / m.downside_dev : 0.0;

    double peak = start;
    int dd_start = -1, max_dd_dur = 0;
    for (size_t i = 0; i < n; ++i) {
        if (recs[i].nav > peak) { peak = recs[i].nav; dd_start = static_cast<int>(i); }
        double dd = (peak - recs[i].nav) / peak;
        if (dd > m.max_dd) m.max_dd = dd;
        if (dd_start >= 0) max_dd_dur = std::max(max_dd_dur, static_cast<int>(i) - dd_start);
    }
    m.max_dd_duration = static_cast<double>(max_dd_dur);
    m.calmar = m.max_dd > 0.0 ? m.annual_return / m.max_dd : 0.0;
    m.win_rate = rets.empty() ? 0.0 : static_cast<double>(wins) / rets.size();
    m.profit_factor = loss_sum > 0.0 ? win_sum / loss_sum : 0.0;
    return m;
}

void save_chart(const std::vector<DailyRecord>& recs, double initial_cash) {
    namespace mp = matplot;
    size_t n = recs.size();
    std::vector<double> xs(n), nav(n), cum_ret(n), dd(n);
    double peak = initial_cash;
    for (size_t i = 0; i < n; ++i) {
        xs[i] = static_cast<double>(i);
        nav[i] = recs[i].nav;
        cum_ret[i] = (recs[i].nav - initial_cash) / initial_cash;
        if (recs[i].nav > peak) peak = recs[i].nav;
        dd[i] = (peak - recs[i].nav) / peak;
    }

    auto fig = mp::figure(false);
    fig->size(1400, 900);

    auto ax1 = mp::subplot(2, 1, 0);
    ax1->plot(xs, nav, "-")->line_width(2).display_name("NAV");
    ax1->plot({xs.front(), xs.back()}, {initial_cash, initial_cash}, "k--")
        ->line_width(1.5).display_name("Initial Cash");
    ax1->ylabel("Portfolio NAV");
    ax1->title("Live Trading: Portfolio Net Asset Value");
    ax1->legend();
    ax1->grid(mp::on);

    auto ax2 = mp::subplot(2, 1, 1);
    ax2->plot(xs, cum_ret, "-")->line_width(2).display_name("Cumulative Return");
    ax2->plot(xs, dd, "-")->line_width(2).display_name("Drawdown");
    ax2->ylabel("Return / Drawdown");
    ax2->xlabel("Time");
    ax2->title("Cumulative Return vs Drawdown");
    ax2->legend();
    ax2->grid(mp::on);
    fig->save("outputs/week7/live_trade_report_chart.png");
    fmt::print("  图表已保存: outputs/week7/live_trade_report_chart.png\n");
}

void save_text_report(const std::string& path,
                      const std::vector<std::string>& csv_paths,
                      const std::map<std::string, StockResult>& by_stock,
                      const std::vector<DailyRecord>& recs,
                      const Metrics& m,
                      double initial_cash) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return;
    ofs << "# 实盘交易绩效分析报告\n\n";
    ofs << "## 数据源\n";
    for (const auto& p : csv_paths) ofs << "- " << p << "\n";
    ofs << "\n## 账户概要\n";
    ofs << "- 初始资金: " << initial_cash << "\n";
    if (!recs.empty()) {
        ofs << "- 期末净值: " << recs.back().nav << "\n";
        ofs << "- 累计盈亏: " << recs.back().cum_pnl << "\n";
        ofs << "- 交易日数: " << recs.size() << "\n";
    }
    ofs << "- 总交易笔数: " << m.total_trades << "\n";
    ofs << "- 总收益率: " << fmt::format("{:.2f}%", m.total_return * 100.0) << "\n";
    ofs << "- 年化收益率: " << fmt::format("{:.2f}%", m.annual_return * 100.0) << "\n";
    ofs << "- 年化波动率: " << fmt::format("{:.2f}%", m.volatility * 100.0) << "\n";
    ofs << "- 夏普比率: " << fmt::format("{:.3f}", m.sharpe) << "\n";
    ofs << "- 索提诺比率: " << fmt::format("{:.3f}", m.sortino) << "\n";
    ofs << "- 最大回撤: " << fmt::format("{:.2f}%", m.max_dd * 100.0) << "\n";
    ofs << "- 最大回撤持续交易日: " << m.max_dd_duration << "\n";
    ofs << "- Calmar: " << fmt::format("{:.3f}", m.calmar) << "\n";
    ofs << "- 日胜率: " << fmt::format("{:.1f}%", m.win_rate * 100.0) << "\n";
    ofs << "- 盈亏比: " << fmt::format("{:.3f}", m.profit_factor) << "\n";
    ofs << "- 日均盈亏: " << fmt::format("{:.2f}", m.avg_daily_pnl) << "\n";

    ofs << "\n## 个股分析\n";
    ofs << "| 代码 | 名称 | 买入次数 | 卖出次数 | 买入金额 | 卖出金额 | 已实现盈亏 | 剩余持仓 |\n";
    ofs << "|------|------|----------|----------|----------|----------|------------|----------|\n";
    for (const auto& kv : by_stock) {
        const auto& sr = kv.second;
        ofs << fmt::format("| {} | {} | {} | {} | {:.2f} | {:.2f} | {:.2f} | {:.0f} |\n",
                           sr.code, sr.name, sr.buy_count, sr.sell_count,
                           sr.buy_amount, sr.sell_amount, sr.realized_pnl, sr.remaining_qty);
    }
    fmt::print("  文本报告已保存: {}\n", path);
}

void save_nav_csv(const std::string& path, const std::vector<DailyRecord>& recs) {
    quant::csv::write_csv(path,
                          {"date", "cash", "market_value", "nav", "daily_pnl", "cum_pnl"},
                          [&]() {
                              std::vector<std::vector<std::string>> rows;
                              rows.reserve(recs.size());
                              for (const auto& r : recs) {
                                  rows.push_back({r.date,
                                                  fmt::format("{:.4f}", r.cash),
                                                  fmt::format("{:.4f}", r.market_value),
                                                  fmt::format("{:.4f}", r.nav),
                                                  fmt::format("{:.4f}", r.daily_pnl),
                                                  fmt::format("{:.4f}", r.cum_pnl)});
                              }
                              return rows;
                          }());
    fmt::print("  NAV CSV已保存: {}\n", path);
}

} // namespace

int main(int argc, char* argv[]) {
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    std::vector<std::string> csv_paths;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) csv_paths.push_back(argv[i]);
    } else {
        // Default: look for any trade CSV in data/.
        csv_paths = {"data/trade_history.csv"};
    }

    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("  实盘交易绩效分析\n");
    fmt::print("{0}\n", std::string(80, '='));

    std::filesystem::create_directories("outputs/week7");

    fmt::print("\n第1步: 读取券商成交记录 CSV\n");
    auto trades = load_broker_csvs(csv_paths);
    if (trades.empty()) {
        fmt::print("  未读取到任何成交记录, 程序退出\n");
        fmt::print("  用法: {} <broker_trade1.csv> [broker_trade2.csv ...]\n", argv[0]);
        return 0;
    }
    fmt::print("  共 {} 条有效成交记录\n", trades.size());
    fmt::print("  时间范围: {} ~ {}\n", trades.front().date, trades.back().date);

    fmt::print("\n第2步: 按个股统计\n");
    auto by_stock = analyze_by_stock(trades);
    fmt::print("  {:12s} {:8s} {:>5} {:>5} {:>12} {:>12} {:>12} {:>10}\n",
               "代码", "名称", "买", "卖", "买入金额", "卖出金额", "实现盈亏", "剩余持仓");
    for (const auto& kv : by_stock) {
        const auto& sr = kv.second;
        fmt::print("  {:12s} {:8s} {:>5} {:>5} {:>12.2f} {:>12.2f} {:>+12.2f} {:>10.0f}\n",
                   sr.code, sr.name, sr.buy_count, sr.sell_count,
                   sr.buy_amount, sr.sell_amount, sr.realized_pnl, sr.remaining_qty);
    }

    fmt::print("\n第3步: 构建组合每日净值曲线\n");
    const double initial_cash = 1'000'000.0;
    std::map<std::string, double> final_positions, final_cost_basis;
    auto recs = build_portfolio_nav(trades, cfg, initial_cash, final_positions, final_cost_basis);
    if (recs.empty()) {
        fmt::print("  净值曲线为空, 程序退出\n");
        return 1;
    }
    fmt::print("  初始资金: {:.2f}\n", initial_cash);
    fmt::print("  期末净值: {:.2f}\n", recs.back().nav);
    fmt::print("  累计盈亏: {:.2f}\n", recs.back().cum_pnl);
    fmt::print("  交易日数: {}\n", recs.size());

    fmt::print("\n第4步: 计算绩效指标\n");
    Metrics m = calculate_metrics(recs, initial_cash);
    m.total_trades = trades.size();
    fmt::print("  总收益率:        {:>+10.2f}%\n", m.total_return * 100.0);
    fmt::print("  年化收益率:      {:>+10.2f}%\n", m.annual_return * 100.0);
    fmt::print("  年化波动率:      {:>10.2f}%\n", m.volatility * 100.0);
    fmt::print("  夏普比率:        {:>+10.3f}\n", m.sharpe);
    fmt::print("  索提诺比率:      {:>+10.3f}\n", m.sortino);
    fmt::print("  最大回撤:        {:>10.2f}%\n", m.max_dd * 100.0);
    fmt::print("  最大回撤持续:    {:>10.0f} 交易日\n", m.max_dd_duration);
    fmt::print("  Calmar比率:      {:>+10.3f}\n", m.calmar);
    fmt::print("  日胜率:          {:>10.1f}%\n", m.win_rate * 100.0);
    fmt::print("  盈亏比:          {:>10.3f}\n", m.profit_factor);
    fmt::print("  日均盈亏:        {:>+10.2f}\n", m.avg_daily_pnl);

    fmt::print("\n第5步: 生成图表与报告\n");
    save_chart(recs, initial_cash);
    save_text_report("outputs/week7/live_trade_report.txt", csv_paths, by_stock, recs, m, initial_cash);
    save_nav_csv("outputs/week7/live_trade_report_nav.csv", recs);

    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("  实盘交易绩效分析完成\n");
    fmt::print("{0}\n", std::string(80, '='));
    return 0;
}
