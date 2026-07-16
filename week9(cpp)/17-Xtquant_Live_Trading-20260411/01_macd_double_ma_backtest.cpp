// 17-Xtquant实盘与Agent搭建实战 / strategy-backtest/scripts/run_backtest.py 的 C++ 实现
// 支持 MACD 与双均线策略回测，输出绩效报告、JSON 与图表。

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <matplot/matplot.h>
#include <nlohmann/json.hpp>

#include "backtest.hpp"
#include "backtest_data.hpp"
#include "backtest_data_mysql.hpp"
#include "env.hpp"
#include "indicators.hpp"

using json = nlohmann::json;

namespace {

struct Signal {
    std::string date;
    std::string action; // buy / sell
    double price = 0.0;
    std::string reason;
};

struct TradeStats {
    int trade_count = 0;
    int win_count = 0;
    double win_rate = 0.0;
    double total_return = 0.0;
    double max_drawdown = 0.0;
    std::vector<Signal> recent_trades;
};

std::string digits_only(const std::string& s) {
    std::string out;
    for (char c : s) if (c >= '0' && c <= '9') out.push_back(c);
    return out;
}

std::string to_full_code(const std::string& code) {
    if (code.size() == 9 && code[6] == '.') return code;
    std::string digits = digits_only(code);
    if (digits.size() != 6) return code;
    if (digits[0] == '0' || digits[0] == '3') return digits + ".SZ";
    return digits + ".SH";
}

std::string csv_path_for(const std::string& code, const std::string& data_dir) {
    std::string full = to_full_code(code);
    std::string digits = digits_only(full);
    std::string suffix = full.size() == 9 ? full.substr(7, 2) : "SH";
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::toupper);
    std::string dir = data_dir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
    return dir + digits + "_" + suffix + "_daily.csv";
}

std::vector<quant::bt::Bar> load_bars(const std::string& code,
                                      const std::string& data_dir,
                                      const std::string& start_date,
                                      const std::string& end_date,
                                      bool use_mysql,
                                      const quant::mysql::Config& cfg,
                                      size_t count) {
    std::vector<quant::bt::Bar> bars;
    // Try CSV first.
    std::string path = csv_path_for(code, data_dir);
    if (std::filesystem::exists(path)) {
        bars = quant::bt::data::load_from_csv(path, start_date, end_date);
    }
    if (bars.empty() && use_mysql) {
        bars = quant::bt::data::load_from_mysql(cfg, to_full_code(code), start_date, end_date);
    }
    if (count > 0 && bars.size() > count) {
        bars.erase(bars.begin(), bars.end() - count);
    }
    return bars;
}

std::vector<Signal> generate_macd_signals(const std::vector<quant::bt::Bar>& bars) {
    std::vector<double> closes;
    for (const auto& b : bars) closes.push_back(b.close);
    auto macd = quant::ind::macd(closes, 12, 26, 9);
    std::vector<Signal> signals;
    int position = 0;
    for (size_t i = 1; i < bars.size(); ++i) {
        if (std::isnan(macd.dif[i]) || std::isnan(macd.dea[i])) continue;
        double prev_dif = macd.dif[i - 1];
        double prev_dea = macd.dea[i - 1];
        double curr_dif = macd.dif[i];
        double curr_dea = macd.dea[i];
        if (prev_dif <= prev_dea && curr_dif > curr_dea && position == 0) {
            signals.push_back({bars[i].date, "buy", bars[i].close, "MACD金叉"});
            position = 1;
        } else if (prev_dif >= prev_dea && curr_dif < curr_dea && position == 1) {
            signals.push_back({bars[i].date, "sell", bars[i].close, "MACD死叉"});
            position = 0;
        }
    }
    return signals;
}

std::vector<Signal> generate_double_ma_signals(const std::vector<quant::bt::Bar>& bars,
                                               size_t short_window = 5,
                                               size_t long_window = 20) {
    std::vector<double> closes;
    for (const auto& b : bars) closes.push_back(b.close);
    auto ma_short = quant::ind::sma(closes, short_window);
    auto ma_long = quant::ind::sma(closes, long_window);
    std::vector<Signal> signals;
    int position = 0;
    for (size_t i = 1; i < bars.size(); ++i) {
        if (std::isnan(ma_short[i]) || std::isnan(ma_long[i])) continue;
        double prev_s = ma_short[i - 1];
        double prev_l = ma_long[i - 1];
        double curr_s = ma_short[i];
        double curr_l = ma_long[i];
        if (prev_s <= prev_l && curr_s > curr_l && position == 0) {
            signals.push_back({bars[i].date, "buy", bars[i].close,
                               fmt::format("MA{}上穿MA{}", short_window, long_window)});
            position = 1;
        } else if (prev_s >= prev_l && curr_s < curr_l && position == 1) {
            signals.push_back({bars[i].date, "sell", bars[i].close,
                               fmt::format("MA{}下穿MA{}", short_window, long_window)});
            position = 0;
        }
    }
    return signals;
}

std::string detect_latest_signal(const std::vector<quant::bt::Bar>& bars,
                                 const std::string& strategy) {
    if (bars.size() < 2) return "none";
    std::vector<double> closes;
    for (const auto& b : bars) closes.push_back(b.close);
    if (strategy == "macd") {
        auto macd = quant::ind::macd(closes, 12, 26, 9);
        size_t n = macd.dif.size();
        if (n < 2) return "none";
        double pd = macd.dif[n - 2], cd = macd.dif[n - 1];
        double pa = macd.dea[n - 2], ca = macd.dea[n - 1];
        if (pd <= pa && cd > ca) return "golden_cross";
        if (pd >= pa && cd < ca) return "death_cross";
        return cd > ca ? "bullish" : "bearish";
    } else {
        auto ma_short = quant::ind::sma(closes, 5);
        auto ma_long = quant::ind::sma(closes, 20);
        size_t n = ma_short.size();
        if (n < 2) return "none";
        double ps = ma_short[n - 2], pl = ma_long[n - 2];
        double cs = ma_short[n - 1], cl = ma_long[n - 1];
        if (ps <= pl && cs > cl) return "golden_cross";
        if (ps >= pl && cs < cl) return "death_cross";
        return cs > cl ? "bullish" : "bearish";
    }
}

TradeStats compute_stats(const std::vector<Signal>& signals) {
    TradeStats st;
    std::vector<std::pair<std::string, std::string>> trade_pairs; // buy_date, sell_date
    double buy_price = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> returns;
    for (const auto& sig : signals) {
        if (sig.action == "buy") {
            buy_price = sig.price;
        } else if (sig.action == "sell" && !std::isnan(buy_price)) {
            double ret = (sig.price - buy_price) / buy_price;
            returns.push_back(ret);
            buy_price = std::numeric_limits<double>::quiet_NaN();
        }
    }
    if (returns.empty()) return st;
    st.trade_count = static_cast<int>(returns.size());
    st.win_count = static_cast<int>(std::count_if(returns.begin(), returns.end(),
                                                   [](double r) { return r > 0.0; }));
    st.win_rate = static_cast<double>(st.win_count) / st.trade_count * 100.0;

    double cumulative = 1.0;
    double peak = 1.0;
    double max_dd = 0.0;
    for (double r : returns) {
        cumulative *= (1.0 + r);
        peak = std::max(peak, cumulative);
        double dd = (peak - cumulative) / peak;
        max_dd = std::max(max_dd, dd);
    }
    st.total_return = (cumulative - 1.0) * 100.0;
    st.max_drawdown = max_dd * 100.0;

    // Recent up to 10 complete trades (buy->sell pairs), chronological order.
    double bp = std::numeric_limits<double>::quiet_NaN();
    std::string bd;
    for (const auto& s : signals) {
        if (s.action == "buy") {
            bp = s.price;
            bd = s.date;
        } else if (s.action == "sell" && !std::isnan(bp)) {
            double r = (s.price - bp) / bp;
            Signal trade;
            trade.date = bd + " -> " + s.date;
            trade.action = (r > 0.0 ? "win" : "loss");
            trade.price = r * 100.0;
            trade.reason = fmt::format("{:.3f} -> {:.3f}", bp, s.price);
            st.recent_trades.push_back(trade);
            bp = std::numeric_limits<double>::quiet_NaN();
        }
    }
    if (st.recent_trades.size() > 10) {
        st.recent_trades.erase(st.recent_trades.begin(), st.recent_trades.end() - 10);
    }
    return st;
}

void print_report(const std::string& code,
                  const std::string& strategy_name,
                  const std::vector<quant::bt::Bar>& bars,
                  const std::vector<Signal>& signals,
                  const TradeStats& st,
                  const std::string& latest_signal) {
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  {} 策略回测 | {}\n", code, strategy_name);
    fmt::print("{0}\n", std::string(70, '='));
    fmt::print("  数据区间: {} ~ {} (共 {} 根K线)\n",
               bars.front().date, bars.back().date, bars.size());
    fmt::print("  最新收盘价: {:.3f} ({}), 最新信号: {}\n",
               bars.back().close, bars.back().date, latest_signal);
    fmt::print("  交易次数: {} | 盈利次数: {} | 胜率: {:.1f}%\n",
               st.trade_count, st.win_count, st.win_rate);
    fmt::print("  累计收益: {:.2f}% | 最大回撤: {:.2f}%\n",
               st.total_return, st.max_drawdown);
    fmt::print("  信号总数: {} (买入 {} / 卖出 {})\n",
               signals.size(),
               std::count_if(signals.begin(), signals.end(),
                             [](const Signal& s) { return s.action == "buy"; }),
               std::count_if(signals.begin(), signals.end(),
                             [](const Signal& s) { return s.action == "sell"; }));
    if (!st.recent_trades.empty()) {
        fmt::print("\n  最近 10 笔交易:\n");
        for (const auto& t : st.recent_trades) {
            fmt::print("    {} | {} | 收益 {:.2f}% | {}\n",
                       t.date, t.action, t.price, t.reason);
        }
    }
    fmt::print("{0}\n", std::string(70, '='));
}

json to_json(const std::string& code,
             const std::string& strategy_name,
             const std::vector<quant::bt::Bar>& bars,
             const std::vector<Signal>& signals,
             const TradeStats& st,
             const std::string& latest_signal) {
    json j;
    j["stock_code"] = code;
    j["strategy"] = strategy_name;
    j["data_range"] = fmt::format("{} ~ {}", bars.front().date, bars.back().date);
    j["data_count"] = bars.size();
    j["latest_close"] = std::round(bars.back().close * 1000.0) / 1000.0;
    j["latest_date"] = bars.back().date;
    j["latest_signal"] = latest_signal;
    j["trade_count"] = st.trade_count;
    j["win_count"] = st.win_count;
    j["win_rate"] = std::round(st.win_rate * 10.0) / 10.0;
    j["total_return"] = std::round(st.total_return * 100.0) / 100.0;
    j["max_drawdown"] = std::round(st.max_drawdown * 100.0) / 100.0;
    json trades = json::array();
    for (const auto& s : signals) {
        json t;
        t["date"] = s.date;
        t["action"] = s.action;
        t["price"] = std::round(s.price * 1000.0) / 1000.0;
        t["reason"] = s.reason;
        trades.push_back(t);
    }
    j["signals"] = trades;
    json recent = json::array();
    for (const auto& t : st.recent_trades) {
        json r;
        r["period"] = t.date;
        r["result"] = t.action;
        r["return_pct"] = std::round(t.price * 100.0) / 100.0;
        r["prices"] = t.reason;
        recent.push_back(r);
    }
    j["recent_trades"] = recent;
    return j;
}

void save_json(const std::string& path, const json& j) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return;
    ofs << j.dump(2);
    fmt::print("  JSON 已保存: {}\n", path);
}

void save_chart(const std::string& code,
                const std::string& strategy_name,
                const std::vector<quant::bt::Bar>& bars,
                const std::vector<Signal>& signals) {
    namespace mp = matplot;
    auto fig = mp::figure(false);
    fig->size(1400, 700);

    std::vector<double> xs(bars.size()), closes(bars.size());
    for (size_t i = 0; i < bars.size(); ++i) {
        xs[i] = static_cast<double>(i);
        closes[i] = bars[i].close;
    }

    auto ax = fig->current_axes();
    ax->hold(mp::on);
    ax->plot(xs, closes, "-")->line_width(1.5).display_name("收盘价");

    // Buy / sell markers.
    std::vector<double> buy_x, buy_y, sell_x, sell_y;
    for (size_t i = 0; i < bars.size(); ++i) {
        for (const auto& s : signals) {
            if (s.date == bars[i].date) {
                if (s.action == "buy") { buy_x.push_back(xs[i]); buy_y.push_back(s.price); }
                else { sell_x.push_back(xs[i]); sell_y.push_back(s.price); }
            }
        }
    }
    if (!buy_x.empty()) ax->scatter(buy_x, buy_y, 8.0)->display_name("买入");
    if (!sell_x.empty()) ax->scatter(sell_x, sell_y, 8.0)->display_name("卖出");

    std::vector<std::string> labels;
    for (size_t i = 0; i < bars.size(); i += std::max<size_t>(1, bars.size() / 8)) {
        labels.push_back(bars[i].date);
    }
    ax->xticks(xs);
    ax->xticklabels(labels);
    ax->xlabel("日期");
    ax->ylabel("价格");
    ax->title(fmt::format("{} - {} 回测信号", code, strategy_name));
    ax->legend();
    ax->grid(mp::on);

    std::string path = "outputs/week9/" + code + "_" + strategy_name + "_backtest.png";
    std::replace(path.begin(), path.end(), '(', '_');
    std::replace(path.begin(), path.end(), ')', '_');
    std::replace(path.begin(), path.end(), ',', '_');
    fig->save(path);
    fmt::print("  图表已保存: {}\n", path);
}

} // namespace

int main(int argc, char* argv[]) {
    std::string code;
    std::string strategy = "macd";
    std::string start_date;
    std::string end_date;
    std::string data_dir = "data";
    std::string output_json;
    size_t count = 250;
    bool use_mysql = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--code" || arg == "-c") && i + 1 < argc) code = argv[++i];
        else if (arg == "--strategy" && i + 1 < argc) strategy = argv[++i];
        else if (arg == "--start" && i + 1 < argc) start_date = argv[++i];
        else if (arg == "--end" && i + 1 < argc) end_date = argv[++i];
        else if (arg == "--data_dir" && i + 1 < argc) data_dir = argv[++i];
        else if (arg == "--count" && i + 1 < argc) count = static_cast<size_t>(std::atoi(argv[++i]));
        else if (arg == "--output" && i + 1 < argc) output_json = argv[++i];
        else if (arg == "--mysql") use_mysql = true;
    }

    if (code.empty()) {
        fmt::print("用法: {} --code 600519 [--strategy macd|double_ma] [--start YYYYMMDD] [--end YYYYMMDD] [--count N] [--data_dir data] [--mysql] [--output out.json]\n", argv[0]);
        return 0;
    }

    if (strategy != "macd" && strategy != "double_ma") {
        fmt::print("[错误] strategy 必须是 macd 或 double_ma\n");
        return 1;
    }

    std::filesystem::create_directories("outputs/week9");

    auto env = quant::env::find_and_load_dotenv();
    auto cfg = quant::bt::data::load_mysql_config(env);

    fmt::print("[开始] 加载 {} 历史数据...\n", code);
    auto bars = load_bars(code, data_dir, start_date, end_date, use_mysql, cfg, count);
    if (bars.empty()) {
        fmt::print("[错误] 未找到 {} 的数据\n", code);
        return 1;
    }
    fmt::print("[加载] {} 根K线 ({} ~ {})\n", bars.size(), bars.front().date, bars.back().date);

    std::vector<Signal> signals;
    std::string strategy_name;
    if (strategy == "macd") {
        signals = generate_macd_signals(bars);
        strategy_name = "MACD(12,26,9)";
    } else {
        signals = generate_double_ma_signals(bars, 5, 20);
        strategy_name = "双均线(MA5,MA20)";
    }

    auto st = compute_stats(signals);
    auto latest_signal = detect_latest_signal(bars, strategy);
    print_report(to_full_code(code), strategy_name, bars, signals, st, latest_signal);

    auto j = to_json(to_full_code(code), strategy_name, bars, signals, st, latest_signal);
    if (!output_json.empty()) save_json(output_json, j);
    else save_json("outputs/week9/" + digits_only(code) + "_" + strategy + "_backtest.json", j);

    save_chart(to_full_code(code), strategy_name, bars, signals);

    fmt::print("\n[结果] {{\"status\": \"success\", \"code\": \"{}\", \"strategy\": \"{}\", \"trades\": {}}}\n",
               to_full_code(code), strategy, st.trade_count);
    return 0;
}
