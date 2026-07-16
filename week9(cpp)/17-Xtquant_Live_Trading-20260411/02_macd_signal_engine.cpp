// 17-Xtquant实盘与Agent搭建实战 / 5-signal_to_order.py 信号层的 C++ 实现
// 仅计算并输出 MACD 金叉/死叉信号，不涉及实盘下单。

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

std::string check_signal_at(size_t idx,
                            const std::vector<double>& dif,
                            const std::vector<double>& dea) {
    if (idx < 1) return "-";
    double pd = dif[idx - 1], pa = dea[idx - 1];
    double cd = dif[idx], ca = dea[idx];
    if (pd <= pa && cd > ca) return "金叉";
    if (pd >= pa && cd < ca) return "死叉";
    return "-";
}

std::string check_signal(const std::vector<double>& dif, const std::vector<double>& dea) {
    if (dif.size() < 2) return "none";
    return check_signal_at(dif.size() - 1, dif, dea);
}

void print_report(const std::string& code,
                  const std::vector<quant::bt::Bar>& bars,
                  const std::vector<double>& dif,
                  const std::vector<double>& dea,
                  const std::vector<double>& macd_bar,
                  const std::string& latest_signal) {
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  {} MACD 信号检查 (12/26/9)\n", code);
    fmt::print("{0}\n", std::string(70, '='));
    fmt::print("  数据: {} ~ {} ({} 根K线)\n",
               bars.front().date, bars.back().date, bars.size());
    fmt::print("  当前状态: {}\n",
               dif.back() > dea.back() ? "DIF > DEA (多头)" : "DIF < DEA (空头)");
    fmt::print("  最新信号: {}\n", latest_signal);

    fmt::print("\n  最近 5 个交易日:\n");
    fmt::print("  {:>12s}  {:>10s}  {:>10s}  {:>10s}  {:>10s}  {:>6s}\n",
               "日期", "收盘价", "DIF", "DEA", "MACD柱", "信号");
    size_t start = bars.size() > 5 ? bars.size() - 5 : 0;
    for (size_t i = start; i < bars.size(); ++i) {
        std::string sig = check_signal_at(i, dif, dea);
        fmt::print("  {:>12s}  {:>10.3f}  {:>10.6f}  {:>10.6f}  {:>10.6f}  {:>6s}\n",
                   bars[i].date, bars[i].close, dif[i], dea[i], macd_bar[i], sig);
    }
    fmt::print("{0}\n", std::string(70, '='));
}

json to_json(const std::string& code,
             const std::vector<quant::bt::Bar>& bars,
             const std::vector<double>& dif,
             const std::vector<double>& dea,
             const std::vector<double>& macd_bar,
             const std::string& latest_signal) {
    json j;
    j["stock_code"] = code;
    j["macd_params"] = "12,26,9";
    j["latest_date"] = bars.back().date;
    j["latest_close"] = std::round(bars.back().close * 1000.0) / 1000.0;
    j["latest_dif"] = std::round(dif.back() * 1e6) / 1e6;
    j["latest_dea"] = std::round(dea.back() * 1e6) / 1e6;
    j["latest_macd_bar"] = std::round(macd_bar.back() * 1e6) / 1e6;
    j["latest_signal"] = latest_signal;
    j["status"] = dif.back() > dea.back() ? "bullish" : "bearish";
    json history = json::array();
    for (size_t i = 0; i < bars.size(); ++i) {
        json h;
        h["date"] = bars[i].date;
        h["close"] = std::round(bars[i].close * 1000.0) / 1000.0;
        h["dif"] = std::round(dif[i] * 1e6) / 1e6;
        h["dea"] = std::round(dea[i] * 1e6) / 1e6;
        h["macd_bar"] = std::round(macd_bar[i] * 1e6) / 1e6;
        h["signal"] = check_signal_at(i, dif, dea);
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
                const std::vector<double>& dif,
                const std::vector<double>& dea,
                const std::vector<double>& macd_bar) {
    namespace mp = matplot;
    auto fig = mp::figure(false);
    fig->size(1400, 900);

    std::vector<double> xs(bars.size()), closes(bars.size());
    for (size_t i = 0; i < bars.size(); ++i) {
        xs[i] = static_cast<double>(i);
        closes[i] = bars[i].close;
    }

    // Subplot 1: price + DIF/DEA on secondary axis not easy; plot MACD in second subplot.
    auto ax1 = mp::subplot(2, 1, 0);
    ax1->hold(mp::on);
    ax1->plot(xs, closes, "-")->line_width(1.5).display_name("收盘价");
    ax1->plot(xs, dif, "-")->line_width(1.0).display_name("DIF");
    ax1->plot(xs, dea, "-")->line_width(1.0).display_name("DEA");
    ax1->xticks(xs);
    std::vector<std::string> labels;
    for (size_t i = 0; i < bars.size(); i += std::max<size_t>(1, bars.size() / 8)) labels.push_back(bars[i].date);
    ax1->xticklabels(labels);
    ax1->xlabel("日期");
    ax1->ylabel("价格 / 指标");
    ax1->title(fmt::format("{} - MACD 信号", code));
    ax1->legend();
    ax1->grid(mp::on);

    auto ax2 = mp::subplot(2, 1, 1);
    ax2->hold(mp::on);
    std::vector<double> positive(macd_bar.size(), 0.0), negative(macd_bar.size(), 0.0);
    for (size_t i = 0; i < macd_bar.size(); ++i) {
        if (macd_bar[i] >= 0.0) positive[i] = macd_bar[i];
        else negative[i] = macd_bar[i];
    }
    ax2->bar(xs, positive)->display_name("MACD+");
    ax2->bar(xs, negative)->display_name("MACD-");
    ax2->xticks(xs);
    ax2->xticklabels(labels);
    ax2->xlabel("日期");
    ax2->ylabel("MACD 柱");
    ax2->legend();
    ax2->grid(mp::on);

    std::string path = "outputs/week9/" + digits_only(code) + "_macd_signal_engine.png";
    fig->save(path);
    fmt::print("  图表已保存: {}\n", path);
}

} // namespace

int main(int argc, char* argv[]) {
    std::string code;
    std::string start_date;
    std::string end_date;
    std::string data_dir = "data";
    std::string output_json;
    size_t count = 250;
    bool use_mysql = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--code" || arg == "-c") && i + 1 < argc) code = argv[++i];
        else if (arg == "--start" && i + 1 < argc) start_date = argv[++i];
        else if (arg == "--end" && i + 1 < argc) end_date = argv[++i];
        else if (arg == "--data_dir" && i + 1 < argc) data_dir = argv[++i];
        else if (arg == "--count" && i + 1 < argc) count = static_cast<size_t>(std::atoi(argv[++i]));
        else if (arg == "--output" && i + 1 < argc) output_json = argv[++i];
        else if (arg == "--mysql") use_mysql = true;
    }

    if (code.empty()) {
        fmt::print("用法: {} --code 600519 [--start YYYYMMDD] [--end YYYYMMDD] [--count N] [--data_dir data] [--mysql] [--output out.json]\n", argv[0]);
        return 0;
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

    std::vector<double> closes;
    for (const auto& b : bars) closes.push_back(b.close);
    auto macd = quant::ind::macd(closes, 12, 26, 9);

    std::string latest_signal = check_signal(macd.dif, macd.dea);
    print_report(to_full_code(code), bars, macd.dif, macd.dea, macd.bar, latest_signal);

    auto j = to_json(to_full_code(code), bars, macd.dif, macd.dea, macd.bar, latest_signal);
    if (!output_json.empty()) save_json(output_json, j);
    else save_json("outputs/week9/" + digits_only(code) + "_macd_signal_engine.json", j);

    save_chart(to_full_code(code), bars, macd.dif, macd.dea, macd.bar);

    fmt::print("\n[结果] {{\"status\": \"success\", \"code\": \"{}\", \"signal\": \"{}\"}}\n",
               to_full_code(code), latest_signal);
    return 0;
}
