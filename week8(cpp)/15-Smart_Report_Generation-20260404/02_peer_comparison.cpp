// 15-智能研报生成 / skills/financial-analysis/scripts/peer_compare.py 的 C++ 实现
// 同行对比分析工具：横向对比多家公司核心财务指标。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <matplot/matplot.h>
#include <nlohmann/json.hpp>

#include "backtest_data_mysql.hpp"
#include "env.hpp"
#include "financial_abstract.hpp"

using json = nlohmann::json;

namespace {

std::string join_strings(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += sep;
        out += v[i];
    }
    return out;
}

struct StockSnapshot {
    std::string code;
    std::string name;
    std::string period; // YYYY-MM-DD
    std::map<std::string, std::pair<double, double>> metrics; // name -> {value, yoy}
    bool has_metric(const std::string& name) const { return metrics.find(name) != metrics.end(); }
};

StockSnapshot load_latest_annual(const std::string& stock_code,
                                 const std::string& data_dir,
                                 bool use_mysql,
                                 const quant::mysql::Config& cfg) {
    StockSnapshot out;
    out.code = quant::finance::digits_only(stock_code);
    out.name = quant::finance::stock_name_for(stock_code);
    auto fa = quant::finance::load_financial_abstract(stock_code, data_dir, use_mysql, cfg, 5);

    for (const auto& kv : fa.metrics) {
        const auto& s = kv.second;
        // Prefer annual 1231 dates.
        std::vector<size_t> annual_idx;
        for (size_t i = 0; i < s.dates.size(); ++i) {
            std::string p;
            for (char c : s.dates[i]) if (c != '-') p.push_back(c);
            if (p.size() == 8 && p.substr(4, 4) == "1231") annual_idx.push_back(i);
        }
        if (annual_idx.empty() && !s.dates.empty()) annual_idx.push_back(0);
        if (annual_idx.empty()) continue;

        size_t latest_i = annual_idx.front();
        double latest_val = s.values[latest_i];
        double yoy = 0.0;
        bool has_yoy = false;
        if (annual_idx.size() >= 2) {
            size_t prev_i = annual_idx[1];
            double prev_val = s.values[prev_i];
            if (std::abs(prev_val) > 1e-12) {
                yoy = (latest_val - prev_val) / std::abs(prev_val) * 100.0;
                has_yoy = true;
            }
        }
        out.period = s.dates[latest_i];
        out.metrics[s.name] = {latest_val, has_yoy ? yoy : std::numeric_limits<double>::quiet_NaN()};
    }
    return out;
}

std::vector<std::string> metric_order() {
    return {
        "营业总收入", "归母净利润", "毛利率", "销售净利率",
        "净资产收益率(ROE)", "总资产报酬率(ROA)", "资产负债率",
        "基本每股收益", "每股经营现金流", "期间费用率"};
}

std::string metric_unit(const std::string& name) {
    if (name == "营业总收入" || name == "归母净利润") return "亿元";
    if (name == "基本每股收益" || name == "每股净资产" || name == "每股经营现金流") return "元";
    return "%";
}

bool higher_better(const std::string& name) {
    if (name == "资产负债率" || name == "期间费用率") return false;
    return true;
}

void print_report(const std::vector<StockSnapshot>& stocks) {
    fmt::print("\n{0}\n", std::string(80, '='));
    std::vector<std::string> names;
    for (const auto& s : stocks) names.push_back(s.name);
    fmt::print("  同行对比分析: {}\n", join_strings(names, " vs "));
    fmt::print("{0}\n", std::string(80, '='));

    std::vector<std::string> codes;
    for (const auto& s : stocks) codes.push_back(s.code);

    std::string header = fmt::format("  {:<20}", "指标");
    for (const auto& s : stocks) header += fmt::format(" | {:>12}", s.name);
    fmt::print("{}\n", header);
    fmt::print("  {0}\n", std::string(20 + 15 * stocks.size(), '-'));

    for (const auto& mname : metric_order()) {
        std::string row = fmt::format("  {:<20}", mname);
        std::vector<std::pair<std::string, double>> values;
        for (const auto& s : stocks) {
            auto it = s.metrics.find(mname);
            if (it != s.metrics.end()) {
                double val = it->second.first;
                double yoy = it->second.second;
                std::string yoy_str;
                if (!std::isnan(yoy)) {
                    yoy_str = fmt::format("({:+.1f}%)", yoy);
                }
                row += fmt::format(" | {:>8.2f}{}{}", val, metric_unit(mname), yoy_str);
                values.emplace_back(s.name, val);
            } else {
                row += fmt::format(" | {:>12}", "N/A");
            }
        }
        if (values.size() >= 2) {
            auto best = higher_better(mname)
                            ? *std::max_element(values.begin(), values.end(),
                                                [](const auto& a, const auto& b) { return a.second < b.second; })
                            : *std::min_element(values.begin(), values.end(),
                                                [](const auto& a, const auto& b) { return a.second < b.second; });
            row += fmt::format("  << {}", best.first);
        }
        fmt::print("{}\n", row);
    }

    fmt::print("\n--- 综合评价 ---\n");
    for (const auto& s : stocks) {
        std::vector<std::string> strengths, weaknesses;
        auto get = [&s](const std::string& n) -> double {
            auto it = s.metrics.find(n);
            if (it == s.metrics.end()) return std::numeric_limits<double>::quiet_NaN();
            return it->second.first;
        };
        double roe = get("净资产收益率(ROE)");
        double margin = get("毛利率");
        double debt = get("资产负债率");
        double net_margin = get("销售净利率");
        if (!std::isnan(roe) && roe >= 15.0) strengths.push_back(fmt::format("ROE较高({:.1f}%)", roe));
        if (!std::isnan(margin) && margin >= 50.0) strengths.push_back(fmt::format("毛利率突出({:.1f}%)", margin));
        if (!std::isnan(debt) && debt <= 30.0) strengths.push_back(fmt::format("负债率低({:.1f}%)", debt));
        if (!std::isnan(net_margin) && net_margin >= 30.0) strengths.push_back(fmt::format("净利率高({:.1f}%)", net_margin));
        if (!std::isnan(roe) && roe < 8.0) weaknesses.push_back(fmt::format("ROE偏低({:.1f}%)", roe));
        if (!std::isnan(debt) && debt > 60.0) weaknesses.push_back(fmt::format("负债率偏高({:.1f}%)", debt));
        fmt::print("  {}: 优势: {}; 关注: {}\n",
                   s.name,
                   strengths.empty() ? "无明显亮点" : join_strings(strengths, "、"),
                   weaknesses.empty() ? "无明显短板" : join_strings(weaknesses, "、"));
    }
    fmt::print("{0}\n", std::string(80, '='));
}

json to_json(const std::vector<StockSnapshot>& stocks) {
    json j;
    j["stocks"] = json::array();
    for (const auto& s : stocks) {
        json sj;
        sj["code"] = s.code;
        sj["name"] = s.name;
        sj["period"] = s.period;
        for (const auto& kv : s.metrics) {
            json mj;
            mj["value"] = kv.second.first;
            mj["yoy"] = std::isnan(kv.second.second) ? json(nullptr) : json(kv.second.second);
            sj["metrics"][kv.first] = mj;
        }
        j["stocks"].push_back(sj);
    }
    return j;
}

void save_json(const std::string& path, const json& j) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return;
    ofs << j.dump(2);
    fmt::print("  JSON 已保存: {}\n", path);
}

void save_chart(const std::vector<StockSnapshot>& stocks) {
    namespace mp = matplot;
    // Select a subset of metrics for grouped bar chart.
    std::vector<std::string> chart_metrics = {
        "毛利率", "销售净利率", "净资产收益率(ROE)", "总资产报酬率(ROA)", "资产负债率"};
    std::vector<std::string> metric_labels;
    std::vector<std::vector<double>> data; // per metric, per stock
    for (const auto& m : chart_metrics) {
        std::vector<double> row;
        bool any = false;
        for (const auto& s : stocks) {
            auto it = s.metrics.find(m);
            if (it != s.metrics.end()) { row.push_back(it->second.first); any = true; }
            else row.push_back(0.0);
        }
        if (any) {
            metric_labels.push_back(m);
            data.push_back(row);
        }
    }
    if (data.empty()) return;

    auto fig = mp::figure(false);
    fig->size(1200, 600);
    auto ax = fig->current_axes();
    ax->hold(mp::on);

    size_t n_metrics = data.size();
    size_t n_stocks = stocks.size();
    double group_width = 0.8;
    double bar_width = group_width / static_cast<double>(n_stocks);
    std::vector<double> xs(n_metrics);
    for (size_t i = 0; i < n_metrics; ++i) xs[i] = static_cast<double>(i);

    for (size_t s = 0; s < n_stocks; ++s) {
        std::vector<double> ys;
        ys.reserve(n_metrics);
        for (size_t m = 0; m < n_metrics; ++m) ys.push_back(data[m][s]);
        std::vector<double> xpos;
        for (size_t m = 0; m < n_metrics; ++m) {
            xpos.push_back(xs[m] + (static_cast<double>(s) - static_cast<double>(n_stocks - 1) / 2.0) * bar_width);
        }
        auto b = ax->bar(xpos, ys);
        b->bar_width(bar_width);
        b->display_name(stocks[s].name);
    }
    ax->xticks(xs);
    ax->xticklabels(metric_labels);
    ax->ylabel("%");
    ax->title("同行核心财务指标对比");
    ax->legend();
    ax->grid(mp::on);
    std::string path = "outputs/week8/peer_comparison.png";
    fig->save(path);
    fmt::print("  图表已保存: {}\n", path);
}

} // namespace

int main(int argc, char* argv[]) {
    std::string stocks_arg;
    std::string data_dir = "week8/15-智能研报生成-20260404/data/financial_data";
    std::string output_json;
    bool use_mysql = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--stocks" && i + 1 < argc) stocks_arg = argv[++i];
        else if (arg == "--data_dir" && i + 1 < argc) data_dir = argv[++i];
        else if (arg == "--output" && i + 1 < argc) output_json = argv[++i];
        else if (arg == "--mysql") use_mysql = true;
    }

    if (stocks_arg.empty()) {
        fmt::print("用法: {} --stocks 600519,000858 [--data_dir ...] [--mysql] [--output out.json]\n", argv[0]);
        return 0;
    }

    std::vector<std::string> stock_codes;
    size_t start = 0;
    for (size_t i = 0; i <= stocks_arg.size(); ++i) {
        if (i == stocks_arg.size() || stocks_arg[i] == ',') {
            std::string token = stocks_arg.substr(start, i - start);
            if (!token.empty()) stock_codes.push_back(token);
            start = i + 1;
        }
    }
    if (stock_codes.size() < 2) {
        fmt::print("[错误] 至少需要 2 家公司的数据才能对比\n");
        return 1;
    }

    std::filesystem::create_directories("outputs/week8");

    auto env = quant::env::find_and_load_dotenv();
    auto cfg = quant::bt::data::load_mysql_config(env);

    fmt::print("[开始] 对比分析: {}\n", join_strings(stock_codes, ", "));
    std::vector<StockSnapshot> stocks;
    std::vector<std::string> missing;
    for (const auto& code : stock_codes) {
        auto snap = load_latest_annual(code, data_dir, use_mysql, cfg);
        if (!snap.metrics.empty()) {
            fmt::print("  {} ({}): 已加载, 数据期间 {}\n", code, snap.name, snap.period);
            stocks.push_back(std::move(snap));
        } else {
            fmt::print("  {}: 数据缺失\n", code);
            missing.push_back(code);
        }
    }

    if (stocks.size() < 2) {
        fmt::print("[错误] 有效数据不足 2 家公司，无法对比\n");
        return 1;
    }

    print_report(stocks);

    auto j = to_json(stocks);
    if (!output_json.empty()) save_json(output_json, j);
    else save_json("outputs/week8/peer_comparison.json", j);
    save_chart(stocks);

    fmt::print("\n[结果] {{\"status\": \"success\", \"stocks\": [{}], \"missing\": [{}]}}\n",
               join_strings(stock_codes, ", "), join_strings(missing, ", "));
    return 0;
}
