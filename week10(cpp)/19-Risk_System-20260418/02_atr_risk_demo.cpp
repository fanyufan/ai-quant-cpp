// 19-强化学习与风控体系 / CASE-Kris的风控体系/2-ATR风控实战.py 的 C++ 实现
// ATR(20) Wilder 平滑、仓位建议、ATR 止损 vs 固定 5% 止损对比

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
#include "risk_engine.hpp"

using json = nlohmann::json;

namespace {

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
                                      const quant::mysql::Config& cfg) {
    std::vector<quant::bt::Bar> bars;
    std::string path = csv_path_for(code, data_dir);
    if (std::filesystem::exists(path)) {
        bars = quant::bt::data::load_from_csv(path, start_date, end_date);
    }
    if (bars.empty() && use_mysql) {
        bars = quant::bt::data::load_from_mysql(cfg, to_full_code(code), start_date, end_date);
    }
    return bars;
}

std::vector<double> calc_atr_wilder(const std::vector<quant::bt::Bar>& bars, size_t period = 20) {
    size_t n = bars.size();
    std::vector<double> atr(n, std::numeric_limits<double>::quiet_NaN());
    if (n == 0 || period == 0) return atr;

    std::vector<double> tr(n, 0.0);
    tr[0] = bars[0].high - bars[0].low;
    for (size_t i = 1; i < n; ++i) {
        double tr1 = bars[i].high - bars[i].low;
        double tr2 = std::abs(bars[i].high - bars[i - 1].close);
        double tr3 = std::abs(bars[i].low - bars[i - 1].close);
        tr[i] = std::max(tr1, std::max(tr2, tr3));
    }

    if (n >= period) {
        double sum = 0.0;
        for (size_t i = 0; i < period; ++i) sum += tr[i];
        atr[period - 1] = sum / static_cast<double>(period);
        for (size_t i = period; i < n; ++i) {
            atr[i] = (atr[i - 1] * static_cast<double>(period - 1) + tr[i]) / static_cast<double>(period);
        }
    }
    return atr;
}

struct Demo1Row {
    std::string label;
    std::string date;
    double close = 0.0;
    double atr = 0.0;
    double suggested = 0.0;
    double atr_stop = 0.0;
    std::string kris_decision;
};

std::vector<Demo1Row> demo_position_sizing(const std::vector<quant::bt::Bar>& bars,
                                           const std::vector<double>& atr,
                                           const std::string& code,
                                           double total_asset) {
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  [Demo 1] ATR 仓位: 高波动期 vs 低波动期 -- {}\n", code);
    fmt::print("{0}\n", std::string(70, '='));

    // Last 120 valid ATR days
    size_t valid_count = 0;
    for (size_t i = bars.size(); i-- > 0;) {
        if (!std::isnan(atr[i])) {
            ++valid_count;
            if (valid_count >= 120) break;
        }
    }
    size_t window_start = bars.size() - valid_count;

    size_t low_idx = window_start;
    size_t high_idx = window_start;
    for (size_t i = window_start; i < bars.size(); ++i) {
        if (!std::isnan(atr[i])) {
            if (std::isnan(atr[low_idx]) || atr[i] < atr[low_idx]) low_idx = i;
            if (std::isnan(atr[high_idx]) || atr[i] > atr[high_idx]) high_idx = i;
        }
    }

    quant::risk::RiskManagerConfig cfg;
    cfg.pre_trade.atr_risk_pct = 0.01;
    cfg.pre_trade.atr_overshoot_ratio = 2.0;
    quant::risk::RiskManager kris(cfg);
    kris.start_day(total_asset);
    kris.macro().update_vix(18.0);

    std::vector<Demo1Row> rows;
    for (auto [label, idx] : std::vector<std::pair<std::string, size_t>>{{"低波动期", low_idx},
                                                                          {"高波动期", high_idx}}) {
        double a = atr[idx];
        double p = bars[idx].close;
        double suggested = (total_asset * 0.01) / a * p;
        double stop = p - 2.0 * a;

        std::unordered_map<std::string, double> prices = {{code, p}};
        std::unordered_map<std::string, double> atr_map = {{code, a}};
        auto d = kris.approve({code, "buy", 100'000.0, p}, prices, atr_map, total_asset, "");

        Demo1Row row;
        row.label = label;
        row.date = bars[idx].date;
        row.close = p;
        row.atr = a;
        row.suggested = suggested;
        row.atr_stop = stop;
        row.kris_decision = quant::risk::decision_name(d.decision);
        rows.push_back(row);

        fmt::print("  {} | {} 收盘 {:.3f} ATR={:.4f} 建议仓位={:.0f} ATR止损={:.3f} Kris={}\n",
                   label, row.date, p, a, suggested, stop, row.kris_decision);
    }
    return rows;
}

struct StopResult {
    std::string name;
    bool triggered = false;
    std::string trigger_date;
    double trigger_price = 0.0;
    double final_value = 0.0;
    double pnl_pct = 0.0;
};

std::pair<StopResult, StopResult> demo_stop_comparison(const std::vector<quant::bt::Bar>& bars,
                                                       const std::vector<double>& atr,
                                                       int entry_idx,
                                                       double initial_cash) {
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  [Demo 2] ATR 止损 vs 固定 5% 止损\n");
    fmt::print("{0}\n", std::string(70, '='));

    // Find valid entry index
    size_t n = bars.size();
    size_t valid_entry = n;
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(atr[i])) {
            --entry_idx;
            if (entry_idx < 0) {
                valid_entry = i;
                break;
            }
        }
    }
    if (valid_entry >= n - 5) {
        // fallback
        for (size_t i = n; i-- > 0;) {
            if (!std::isnan(atr[i]) && i < n - 5) { valid_entry = i; break; }
        }
    }

    double entry_price = bars[valid_entry].close;
    double entry_atr = atr[valid_entry];
    double atr_stop_price = entry_price - 2.0 * entry_atr;
    double fixed_stop_price = entry_price * 0.95;
    int shares = static_cast<int>(initial_cash / entry_price / 100.0) * 100;

    fmt::print("  入场日期: {} 入场价: {:.3f} ATR: {:.4f}\n",
               bars[valid_entry].date, entry_price, entry_atr);
    fmt::print("  ATR止损: {:.3f}  固定5%止损: {:.3f}  股数: {}\n",
               atr_stop_price, fixed_stop_price, shares);

    auto simulate = [&](double stop_price, const std::string& name) -> StopResult {
        StopResult r;
        r.name = name;
        for (size_t i = valid_entry + 1; i < n; ++i) {
            if (bars[i].low <= stop_price) {
                r.triggered = true;
                r.trigger_date = bars[i].date;
                r.trigger_price = stop_price;
                r.final_value = shares * stop_price;
                r.pnl_pct = (stop_price - entry_price) / entry_price;
                return r;
            }
        }
        double end_price = bars.back().close;
        r.final_value = shares * end_price;
        r.pnl_pct = (end_price - entry_price) / entry_price;
        return r;
    };

    auto atr_r = simulate(atr_stop_price, "ATR止损");
    auto fixed_r = simulate(fixed_stop_price, "固定5%止损");

    auto print = [&](const StopResult& r) {
        if (r.triggered) {
            fmt::print("  [{}] 于 {} 触发, 成交价 {:.3f}, 实亏 {:.2f}%, 剩余金额 {:.0f}\n",
                       r.name, r.trigger_date, r.trigger_price, r.pnl_pct * 100.0, r.final_value);
        } else {
            fmt::print("  [{}] 全程未触发, 持有到末日 {}, 浮盈 {:.2f}%, 持仓金额 {:.0f}\n",
                       r.name, bars.back().date, r.pnl_pct * 100.0, r.final_value);
        }
    };
    print(atr_r);
    print(fixed_r);
    return {atr_r, fixed_r};
}

void demo_kris_stop_loop(const std::vector<quant::bt::Bar>& bars,
                         const std::vector<double>& atr,
                         const std::string& code) {
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  [Demo 3] ATR 止损在 Kris 中的调用方式 -- {}\n", code);
    fmt::print("{0}\n", std::string(70, '='));

    size_t n = bars.size();
    size_t entry_idx = n > 10 ? n - 9 : n - std::min<size_t>(n - 1, 5);
    while (entry_idx > 0 && std::isnan(atr[entry_idx])) --entry_idx;

    double entry_price = bars[entry_idx].close;
    double atr_value = atr[entry_idx];
    double stop_price = entry_price - 2.0 * atr_value;

    quant::risk::RiskManager kris;
    kris.start_day(1'000'000.0);
    kris.macro().update_vix(18.0);
    kris.register_position(code, entry_price, atr_value);

    fmt::print("  [入场 {}] {} @ {:.3f}, ATR={:.3f}, 止损价={:.3f}\n",
               bars[entry_idx].date, code, entry_price, atr_value, stop_price);

    bool triggered = false;
    for (size_t i = entry_idx + 1; i < n; ++i) {
        auto d = kris.check_atr_stop(code, bars[i].low);
        if (d.rule_name == "ATR止损") {
            fmt::print("  [{}] 当日最低 {:.3f} -> {}\n", bars[i].date, bars[i].low, d.to_string());
            kris.remove_position(code);
            triggered = true;
            break;
        } else {
            fmt::print("  [{}] 当日最低 {:.3f}, 未触发止损\n", bars[i].date, bars[i].low);
        }
    }
    if (!triggered) {
        fmt::print("  [说明] 数据末尾 {} 天均未触发, 该段行情趋势平稳\n", n - entry_idx - 1);
    }
}

json to_json(const std::string& code,
             const std::vector<Demo1Row>& demo1,
             const std::pair<StopResult, StopResult>& demo2,
             const std::vector<quant::bt::Bar>& bars,
             const std::vector<double>& atr) {
    json j;
    j["stock_code"] = code;
    j["data_range"] = fmt::format("{} ~ {}", bars.front().date, bars.back().date);
    j["data_count"] = bars.size();

    json d1 = json::array();
    for (const auto& r : demo1) {
        json row;
        row["scenario"] = r.label;
        row["date"] = r.date;
        row["close"] = std::round(r.close * 1000.0) / 1000.0;
        row["atr20"] = std::round(r.atr * 10000.0) / 10000.0;
        row["suggested_position"] = std::round(r.suggested);
        row["atr_stop"] = std::round(r.atr_stop * 1000.0) / 1000.0;
        row["kris_decision"] = r.kris_decision;
        d1.push_back(row);
    }
    j["demo1_position_sizing"] = d1;

    auto pack_stop = [](const StopResult& r) {
        json j;
        j["name"] = r.name;
        j["triggered"] = r.triggered;
        j["trigger_date"] = r.trigger_date;
        j["trigger_price"] = std::round(r.trigger_price * 1000.0) / 1000.0;
        j["final_value"] = std::round(r.final_value);
        j["pnl_pct"] = std::round(r.pnl_pct * 10000.0) / 100.0;
        return j;
    };
    j["demo2_atr_vs_fixed_stop"] = {pack_stop(demo2.first), pack_stop(demo2.second)};

    json history = json::array();
    for (size_t i = 0; i < bars.size(); ++i) {
        json h;
        h["date"] = bars[i].date;
        h["close"] = bars[i].close;
        h["high"] = bars[i].high;
        h["low"] = bars[i].low;
        h["atr20"] = std::isnan(atr[i]) ? json(nullptr) : json(std::round(atr[i] * 10000.0) / 10000.0);
        history.push_back(h);
    }
    j["history"] = history;
    return j;
}

void save_json(const std::string& path, const json& j) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return;
    ofs << j.dump(2);
    fmt::print("  JSON 已保存: {}\n", path);
}

void save_chart(const std::string& code,
                const std::vector<quant::bt::Bar>& bars,
                const std::vector<double>& atr,
                const std::string& entry_date,
                double entry_price_arg,
                double atr_stop_price,
                double fixed_stop_price,
                const StopResult& atr_stop,
                const StopResult& fixed_stop) {
    namespace mp = matplot;
    auto fig = mp::figure(false);
    fig->size(1400, 900);

    // Use last 180 days for plotting
    size_t plot_start = bars.size() > 180 ? bars.size() - 180 : 0;
    size_t plot_n = bars.size() - plot_start;
    std::vector<double> xs(plot_n), closes(plot_n), atrs(plot_n);
    std::vector<std::string> labels;
    for (size_t i = 0; i < plot_n; ++i) {
        xs[i] = static_cast<double>(i);
        closes[i] = bars[plot_start + i].close;
        atrs[i] = std::isnan(atr[plot_start + i]) ? 0.0 : atr[plot_start + i];
        if (i % std::max<size_t>(1, plot_n / 8) == 0) labels.push_back(bars[plot_start + i].date);
    }

    auto ax1 = mp::subplot(2, 1, 0);
    ax1->hold(mp::on);
    ax1->plot(xs, closes, "-")->display_name("收盘价");

    // Find entry x
    double entry_x = -1.0;
    for (size_t i = 0; i < plot_n; ++i) {
        if (bars[plot_start + i].date == entry_date) { entry_x = xs[i]; break; }
    }
    (void)entry_price_arg;
    if (entry_x >= 0.0) {
        std::vector<double> vline_xs = {entry_x, entry_x};
        std::vector<double> vline_ys = {*std::min_element(closes.begin(), closes.end()),
                                        *std::max_element(closes.begin(), closes.end())};
        ax1->plot(vline_xs, vline_ys, "--")->display_name("入场");
        std::vector<double> line_x = {xs.front(), xs.back()};
        ax1->plot(line_x, {atr_stop_price, atr_stop_price}, "-")->display_name("ATR止损");
        ax1->plot(line_x, {fixed_stop_price, fixed_stop_price}, "-")->display_name("固定5%止损");

        // Mark trigger points if within window
        auto mark = [&](const StopResult& r, const std::string& name) {
            if (!r.triggered || r.trigger_date.empty()) return;
            for (size_t i = 0; i < plot_n; ++i) {
                if (bars[plot_start + i].date == r.trigger_date) {
                    ax1->scatter(std::vector<double>{xs[i]}, std::vector<double>{r.trigger_price})
                        ->display_name(name);
                    break;
                }
            }
        };
        mark(atr_stop, "ATR止损触发");
        mark(fixed_stop, "固定止损触发");
    }
    ax1->xticks(xs);
    ax1->xticklabels(labels);
    ax1->ylabel("价格");
    ax1->title(fmt::format("ATR 止损 vs 固定 5% 止损 -- {}", code));
    ax1->legend();
    ax1->grid(mp::on);

    auto ax2 = mp::subplot(2, 1, 1);
    ax2->hold(mp::on);
    ax2->plot(xs, atrs, "-")->display_name("ATR(20)");
    ax2->xticks(xs);
    ax2->xticklabels(labels);
    ax2->xlabel("日期");
    ax2->ylabel("ATR");
    ax2->legend();
    ax2->grid(mp::on);

    std::string path = "outputs/week10/" + digits_only(code) + "_atr_risk_demo.png";
    fig->save(path);
    fmt::print("  图表已保存: {}\n", path);
}

} // namespace

int main(int argc, char* argv[]) {
    std::string code = "510050";
    std::string start_date;
    std::string end_date;
    std::string data_dir = "data";
    std::string output_json;
    double total_asset = 1'000'000.0;
    double initial_cash = 1'000'000.0;
    bool use_mysql = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--code" || arg == "-c") && i + 1 < argc) code = argv[++i];
        else if (arg == "--start" && i + 1 < argc) start_date = argv[++i];
        else if (arg == "--end" && i + 1 < argc) end_date = argv[++i];
        else if (arg == "--data_dir" && i + 1 < argc) data_dir = argv[++i];
        else if (arg == "--total_asset" && i + 1 < argc) total_asset = std::stod(argv[++i]);
        else if (arg == "--initial_cash" && i + 1 < argc) initial_cash = std::stod(argv[++i]);
        else if (arg == "--output" && i + 1 < argc) output_json = argv[++i];
        else if (arg == "--mysql") use_mysql = true;
    }

    std::filesystem::create_directories("outputs/week10");

    auto env = quant::env::find_and_load_dotenv();
    auto cfg = quant::bt::data::load_mysql_config(env);

    fmt::print("{0}\n", std::string(70, '='));
    fmt::print("  CASE: ATR 风控实战\n");
    fmt::print("  股票代码: {}\n", to_full_code(code));
    fmt::print("{0}\n", std::string(70, '='));

    auto bars = load_bars(code, data_dir, start_date, end_date, use_mysql, cfg);
    if (bars.empty()) {
        fmt::print("[错误] 未找到 {} 的数据\n", code);
        return 1;
    }
    fmt::print("\n  加载 {} 数据 {} 条, {} ~ {}\n",
               to_full_code(code), bars.size(), bars.front().date, bars.back().date);

    auto atr = calc_atr_wilder(bars, 20);

    auto demo1 = demo_position_sizing(bars, atr, to_full_code(code), total_asset);
    auto demo2 = demo_stop_comparison(bars, atr, -180, initial_cash);
    demo_kris_stop_loop(bars, atr, to_full_code(code));

    auto j = to_json(to_full_code(code), demo1, demo2, bars, atr);
    if (!output_json.empty()) save_json(output_json, j);
    else save_json("outputs/week10/" + digits_only(code) + "_atr_risk_demo.json", j);

    // Chart uses demo2 entry info (ATR stop result entry approx)
    // Recompute entry idx used in demo2 for chart annotations
    size_t n = bars.size();
    int cnt = 180;
    size_t entry_idx = n;
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(atr[i])) {
            --cnt;
            if (cnt < 0) { entry_idx = i; break; }
        }
    }
    if (entry_idx >= n - 5) {
        for (size_t i = n; i-- > 0;) {
            if (!std::isnan(atr[i]) && i < n - 5) { entry_idx = i; break; }
        }
    }
    double entry_price = bars[entry_idx].close;
    double entry_atr = atr[entry_idx];
    save_chart(to_full_code(code), bars, atr, bars[entry_idx].date, entry_price,
               entry_price - 2.0 * entry_atr, entry_price * 0.95,
               demo2.first, demo2.second);

    fmt::print("\n[结果] {{\"status\": \"success\", \"code\": \"{}\", \"days\": {}}}\n",
               to_full_code(code), bars.size());
    return 0;
}
