// 15-智能研报生成 / skills/financial-analysis/scripts/ratio_analysis.py 的 C++ 实现
// 核心财务指标分析工具：读取财务摘要 CSV 或 MySQL，输出趋势分析与图表。

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
#include <nlohmann/json.hpp>

#include "backtest_data_mysql.hpp"
#include "env.hpp"
#include "financial_abstract.hpp"

using json = nlohmann::json;

namespace {

struct AnalyzedMetric {
    std::string name;
    std::string unit;
    std::string category;
    std::vector<std::string> dates; // descending
    std::vector<double> values;
    double latest = 0.0;
    double yoy = 0.0;
    bool has_yoy = false;
    std::string trend; // up/down/stable
};

std::string detect_trend(const std::vector<double>& values) {
    if (values.size() < 3) return "stable";
    int increases = 0;
    for (size_t i = 0; i + 1 < values.size(); ++i) {
        if (values[i] > values[i + 1]) ++increases;
    }
    if (increases >= static_cast<int>(values.size()) - 1) return "up";
    if (increases == 0) return "down";
    return "stable";
}

std::vector<AnalyzedMetric> analyze(const quant::finance::FinancialAbstract& fa, int years) {
    std::vector<AnalyzedMetric> out;
    for (const auto& kv : fa.metrics) {
        const auto& s = kv.second;
        // Filter annual dates ending 12-31, descending.
        std::vector<std::string> dates;
        std::vector<double> vals;
        for (size_t i = 0; i < s.dates.size(); ++i) {
            std::string p;
            for (char c : s.dates[i]) if (c != '-') p.push_back(c);
            if (p.size() == 8 && p.substr(4, 4) == "1231") {
                dates.push_back(s.dates[i]);
                vals.push_back(s.values[i]);
            }
        }
        if (dates.empty()) {
            // Fall back to the latest available dates regardless of month.
            dates = s.dates;
            vals = s.values;
        }
        if (dates.size() > static_cast<size_t>(years)) {
            dates.resize(years);
            vals.resize(years);
        }
        if (dates.empty()) continue;

        AnalyzedMetric m;
        m.name = s.name;
        m.unit = s.unit;
        m.category = s.category;
        m.dates = dates;
        m.values = vals;
        m.latest = vals.front();
        m.trend = detect_trend(vals);
        if (vals.size() >= 2 && std::abs(vals[1]) > 1e-12) {
            m.yoy = (vals[0] - vals[1]) / std::abs(vals[1]) * 100.0;
            m.has_yoy = true;
        }
        out.push_back(m);
    }
    return out;
}

std::string trend_label(const std::string& t) {
    if (t == "up") return "上升";
    if (t == "down") return "下降";
    return "平稳";
}

void print_report(const std::string& code, const std::string& name,
                  const std::vector<AnalyzedMetric>& metrics) {
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  {} ({}) 核心财务指标分析\n", name, code);
    fmt::print("{0}\n", std::string(70, '='));

    std::map<std::string, std::vector<AnalyzedMetric>> by_cat;
    for (const auto& m : metrics) by_cat[m.category].push_back(m);

    std::vector<std::string> cat_order = {"规模", "盈利", "风险", "每股", "现金流", "效率"};
    for (const auto& cat : cat_order) {
        auto it = by_cat.find(cat);
        if (it == by_cat.end()) continue;
        fmt::print("\n--- {}指标 ---\n", cat);
        for (const auto& m : it->second) {
            std::string yoy_str;
            if (m.has_yoy) {
                yoy_str = fmt::format(" (同比 {:+.2f}%)", m.yoy);
            }
            fmt::print("  {}: {:.2f} {} [{}]{}{}\n",
                       m.name, m.latest, m.unit, trend_label(m.trend), yoy_str, "");
            if (m.values.size() > 1) {
                std::string hist;
                for (size_t i = 0; i < m.dates.size(); ++i) {
                    if (i > 0) hist += " | ";
                    hist += fmt::format("{}: {:.2f}", m.dates[i].substr(0, 4), m.values[i]);
                }
                fmt::print("    历史: {}\n", hist);
            }
        }
    }

    // Comprehensive evaluation
    fmt::print("\n--- 综合评价 ---\n");
    auto find_metric = [&metrics](const std::string& name) -> const AnalyzedMetric* {
        for (const auto& m : metrics) if (m.name == name) return &m;
        return nullptr;
    };
    auto roe = find_metric("净资产收益率(ROE)");
    auto margin = find_metric("毛利率");
    auto debt = find_metric("资产负债率");
    auto revenue = find_metric("营业总收入");

    if (roe) {
        if (roe->latest > 20.0) fmt::print("  盈利能力: 优秀 (ROE {:.2f}% > 20%)\n", roe->latest);
        else if (roe->latest > 10.0) fmt::print("  盈利能力: 良好 (ROE {:.2f}%)\n", roe->latest);
        else fmt::print("  盈利能力: 一般 (ROE {:.2f}%)\n", roe->latest);
    }
    if (margin) {
        fmt::print("  毛利水平: {:.2f}%，趋势{}\n", margin->latest, trend_label(margin->trend));
    }
    if (debt) {
        if (debt->latest < 30.0) fmt::print("  财务安全: 稳健 (负债率 {:.2f}%)\n", debt->latest);
        else if (debt->latest < 60.0) fmt::print("  财务安全: 适中 (负债率 {:.2f}%)\n", debt->latest);
        else fmt::print("  财务安全: 偏高 (负债率 {:.2f}%)\n", debt->latest);
    }
    if (revenue && revenue->has_yoy) {
        if (revenue->yoy > 20.0) fmt::print("  成长性: 高增长 (营收同比 {:+.2f}%)\n", revenue->yoy);
        else if (revenue->yoy > 0.0) fmt::print("  成长性: 稳定增长 (营收同比 {:+.2f}%)\n", revenue->yoy);
        else fmt::print("  成长性: 增速放缓 (营收同比 {:+.2f}%)\n", revenue->yoy);
    }
    fmt::print("{0}\n", std::string(70, '='));
}

json to_json(const std::string& code, const std::string& name, int years,
             const std::vector<AnalyzedMetric>& metrics) {
    json j;
    j["stock"] = code;
    j["stock_name"] = name;
    j["years"] = years;
    for (const auto& m : metrics) {
        json mj;
        mj["unit"] = m.unit;
        mj["category"] = m.category;
        mj["latest"] = m.latest;
        mj["yoy"] = m.has_yoy ? json(m.yoy) : json(nullptr);
        mj["trend"] = m.trend;
        mj["values"] = m.values;
        mj["dates"] = m.dates;
        j["metrics"][m.name] = mj;
    }
    return j;
}

void save_json(const std::string& path, const json& j) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return;
    ofs << j.dump(2);
    fmt::print("  JSON 已保存: {}\n", path);
}

void save_charts(const std::string& code, const std::string& name,
                 const std::vector<AnalyzedMetric>& metrics) {
    namespace mp = matplot;
    std::map<std::string, std::vector<AnalyzedMetric>> by_cat;
    for (const auto& m : metrics) by_cat[m.category].push_back(m);

    int n_plots = 0;
    for (const auto& kv : by_cat) if (!kv.second.empty()) ++n_plots;
    if (n_plots == 0) return;

    auto fig = mp::figure(false);
    fig->size(1400, static_cast<int>(n_plots) * 320);

    int idx = 0;
    std::vector<std::string> cat_order = {"规模", "盈利", "风险", "每股", "现金流", "效率"};
    for (const auto& cat : cat_order) {
        auto it = by_cat.find(cat);
        if (it == by_cat.end()) continue;
        auto ax = mp::subplot(n_plots, 1, idx++);
        ax->hold(mp::on);
        const auto& items = it->second;

        // Use common x-axis labels (years from first metric).
        std::vector<std::string> labels;
        if (!items.empty()) {
            for (const auto& d : items[0].dates) labels.push_back(d.substr(0, 4));
        }
        std::vector<double> xs(labels.size());
        for (size_t i = 0; i < labels.size(); ++i) xs[i] = static_cast<double>(i);

        for (const auto& m : items) {
            // Pad/truncate values to xs size.
            std::vector<double> vals = m.values;
            if (vals.size() < xs.size()) vals.resize(xs.size(), std::numeric_limits<double>::quiet_NaN());
            else if (vals.size() > xs.size()) vals.resize(xs.size());
            ax->plot(xs, vals, "-o")->line_width(1.5).display_name(m.name);
        }
        ax->xticks(xs);
        ax->xticklabels(labels);
        ax->xlabel("年度");
        ax->ylabel(cat);
        ax->title(fmt::format("{} - {}指标趋势", name, cat));
        ax->legend();
        ax->grid(mp::on);
    }
    std::string path = "outputs/week8/" + code + "_financial_ratio_analysis.png";
    fig->save(path);
    fmt::print("  图表已保存: {}\n", path);
}

} // namespace

int main(int argc, char* argv[]) {
    std::string stock_code;
    int years = 5;
    std::string data_dir = "week8/15-智能研报生成-20260404/data/financial_data";
    std::string output_json;
    bool use_mysql = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--stock" || arg == "-s") && i + 1 < argc) stock_code = argv[++i];
        else if (arg == "--years" && i + 1 < argc) years = std::atoi(argv[++i]);
        else if (arg == "--data_dir" && i + 1 < argc) data_dir = argv[++i];
        else if (arg == "--output" && i + 1 < argc) output_json = argv[++i];
        else if (arg == "--mysql") use_mysql = true;
    }

    if (stock_code.empty()) {
        fmt::print("用法: {} --stock 600519 [--years 5] [--data_dir ...] [--mysql] [--output out.json]\n", argv[0]);
        return 0;
    }

    std::filesystem::create_directories("outputs/week8");

    auto env = quant::env::find_and_load_dotenv();
    auto cfg = quant::bt::data::load_mysql_config(env);

    fmt::print("[开始] 分析 {} 近 {} 年财务指标\n", stock_code, years);
    auto fa = quant::finance::load_financial_abstract(stock_code, data_dir, use_mysql, cfg, years);
    if (fa.metrics.empty()) {
        fmt::print("[错误] 未找到 {} 的财务数据\n", stock_code);
        fmt::print("[提示] 请确认 CSV 文件存在，或添加 --mysql 从 MySQL 加载\n");
        return 1;
    }
    fmt::print("[加载] 提取到 {} 个核心指标\n", fa.metrics.size());

    auto metrics = analyze(fa, years);
    print_report(stock_code, fa.stock_name, metrics);

    auto j = to_json(stock_code, fa.stock_name, years, metrics);
    if (!output_json.empty()) {
        save_json(output_json, j);
    } else {
        save_json("outputs/week8/" + stock_code + "_financial_ratio_analysis.json", j);
    }
    save_charts(stock_code, fa.stock_name, metrics);

    fmt::print("\n[结果] {{\"status\": \"success\", \"stock\": \"{}\", \"metrics_count\": {}}}\n",
               stock_code, metrics.size());
    return 0;
}
