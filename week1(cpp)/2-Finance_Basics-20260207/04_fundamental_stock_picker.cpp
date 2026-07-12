// 对应 Python: 4-制定你的基本面选股.py
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include "csv.hpp"

namespace fs = std::filesystem;

struct Config {
    double roe_min = 15.0;
    int roe_consecutive_years = 2;
    double debt_to_assets_max = 50.0;
    double ocf_to_profit_min = 0.8;
    double netprofit_yoy_min = 0.0;
    bool use_netprofit_yoy_when_no_ocf = true;
    bool exclude_finance = true;
    bool exclude_st = true;
    std::vector<std::string> finance_industries = {"银行", "保险", "证券", "多元金融", "信托", "租赁"};
};

std::string end8(const std::string& s) {
    std::string out;
    for (char c : s) if (std::isdigit(c)) out += c;
    if (out.size() >= 8) return out.substr(0, 8);
    return out;
}

bool is_finance(const std::string& industry, const Config& cfg) {
    for (const auto& f : cfg.finance_industries) {
        if (industry.find(f) != std::string::npos) return true;
    }
    return false;
}

bool is_st(const std::string& name) {
    std::string upper;
    for (unsigned char c : name) upper += std::toupper(c);
    return upper.find("ST") != std::string::npos;
}

double to_double(const std::string& s) {
    try { return std::stod(s); } catch (...) { return std::numeric_limits<double>::quiet_NaN(); }
}

bool valid(double v) { return std::isfinite(v); }

std::vector<std::pair<std::string, int>> filter_stocks(const quant::csv::CsvData& stocks,
                                                       const quant::csv::CsvData& fina,
                                                       const Config& cfg) {
    std::vector<std::pair<std::string, int>> funnel;

    std::vector<std::string> ts_codes = stocks.column("ts_code");
    std::vector<std::string> names = stocks.column("name");
    std::vector<std::string> industries = stocks.column("industry");

    // Available fina codes
    std::set<std::string> fina_codes;
    for (const auto& c : fina.column("ts_code")) if (!c.empty()) fina_codes.insert(c);

    funnel.emplace_back("全市场上市股票", static_cast<int>(ts_codes.size()));

    // Build pool rows
    std::vector<int> pool_indices;
    for (size_t i = 0; i < ts_codes.size(); ++i) {
        if (fina_codes.count(ts_codes[i])) pool_indices.push_back(static_cast<int>(i));
    }
    funnel.emplace_back("数据池中有财务数据", static_cast<int>(pool_indices.size()));

    // Exclude ST
    if (cfg.exclude_st) {
        std::vector<int> tmp;
        for (int idx : pool_indices) {
            if (!is_st(names[idx])) tmp.push_back(idx);
        }
        pool_indices = std::move(tmp);
        funnel.emplace_back("排除ST股", static_cast<int>(pool_indices.size()));
    }

    // Exclude finance
    if (cfg.exclude_finance) {
        std::vector<int> tmp;
        for (int idx : pool_indices) {
            if (!is_finance(industries[idx], cfg)) tmp.push_back(idx);
        }
        pool_indices = std::move(tmp);
        funnel.emplace_back("排除金融行业", static_cast<int>(pool_indices.size()));
    }

    std::set<std::string> pool_codes;
    for (int idx : pool_indices) pool_codes.insert(ts_codes[idx]);

    return funnel;  // Caller uses pool_codes for next steps
}

int main() {
    const std::string DATA_DIR = "data";
    Config cfg;

    fmt::print("{:=<70}\n", "");
    fmt::print("CASE：制定你的基本面选股策略\n");
    fmt::print("{:=<70}\n", "");
    fmt::print("筛选条件：\n");
    fmt::print("  [1] ROE > {}%：2023、2024年年报（2期，已完整披露）\n", cfg.roe_min);
    fmt::print("  [2] 资产负债率 < {}%\n", cfg.debt_to_assets_max);
    fmt::print("  [3] 净利润同比增长 > {}%（当前无经营性现金流/净利润数据）\n", cfg.netprofit_yoy_min);
    if (cfg.exclude_finance) {
        fmt::print("  [X] 排除金融行业：");
        for (const auto& s : cfg.finance_industries) fmt::print("{} ", s);
        fmt::print("\n");
    }
    if (cfg.exclude_st) fmt::print("  [X] 排除 ST/*ST 股\n");
    fmt::print("{:=<70}\n", "");

    std::vector<std::string> required = {"stock_basic.csv", "daily_basic_latest.csv", "fina_indicator_pool.csv"};
    std::vector<std::string> missing;
    for (const auto& f : required) {
        if (!fs::exists(DATA_DIR + "/" + f)) missing.push_back(f);
    }
    if (!missing.empty()) {
        fmt::print("错误：缺少数据文件，请先运行数据下载脚本\n");
        fmt::print("缺少的文件：\n");
        for (const auto& f : missing) fmt::print("  {}\n", f);
        return 1;
    }

    auto stocks = quant::csv::read_csv(DATA_DIR + "/stock_basic.csv");
    auto daily = quant::csv::read_csv(DATA_DIR + "/daily_basic_latest.csv");
    auto fina = quant::csv::read_csv(DATA_DIR + "/fina_indicator_pool.csv");

    auto funnel = filter_stocks(stocks, fina, cfg);
    std::set<std::string> pool_codes;
    {
        std::vector<std::string> ts_codes = stocks.column("ts_code");
        std::vector<std::string> names = stocks.column("name");
        std::vector<std::string> industries = stocks.column("industry");
        std::set<std::string> fina_codes;
        for (const auto& c : fina.column("ts_code")) if (!c.empty()) fina_codes.insert(c);

        for (size_t i = 0; i < ts_codes.size(); ++i) {
            if (!fina_codes.count(ts_codes[i])) continue;
            if (cfg.exclude_st && is_st(names[i])) continue;
            if (cfg.exclude_finance && is_finance(industries[i], cfg)) continue;
            pool_codes.insert(ts_codes[i]);
        }
    }

    // Determine target periods
    std::vector<std::string> target_periods;
    std::string period_desc;
    {
        std::set<std::string> annual;
        for (const auto& s : fina.column("end_date")) {
            auto e = end8(s);
            if (e.size() == 8 && e.compare(e.size() - 4, 4, "1231") == 0) annual.insert(e);
        }
        bool has23 = annual.count("20231231");
        bool has24 = annual.count("20241231");
        if (has23 && has24) {
            target_periods = {"20231231", "20241231"};
            period_desc = "2023、2024年年报（2期，已完整披露）";
        } else {
            target_periods.assign(annual.begin(), annual.end());
            if (target_periods.size() >= static_cast<size_t>(cfg.roe_consecutive_years)) {
                target_periods = std::vector<std::string>(target_periods.end() - cfg.roe_consecutive_years, target_periods.end());
                period_desc = fmt::format("连续{}年（{}-{}年年报）", cfg.roe_consecutive_years, target_periods.front().substr(0, 4), target_periods.back().substr(0, 4));
            } else if (!target_periods.empty()) {
                period_desc = fmt::format("最近{}期", target_periods.size());
            } else {
                target_periods = {"20231231", "20241231"};
                period_desc = "默认2023、2024年年报";
            }
        }
    }

    // Filter fina to pool
    std::vector<std::string> fina_codes = fina.column("ts_code");
    std::vector<std::string> fina_end = fina.column("end_date");
    std::vector<std::string> fina_roe = fina.column("roe");
    std::vector<std::string> fina_debt = fina.column("debt_to_assets");
    std::vector<std::string> fina_ocf = fina.column("ocf_to_profit");
    std::vector<std::string> fina_yoy = fina.column("netprofit_yoy");

    std::map<std::string, std::map<std::string, std::vector<std::map<std::string, double>>>> fina_map;
    // Map: ts_code -> end8 -> {roe, debt, ocf, yoy}
    for (size_t i = 0; i < fina_codes.size(); ++i) {
        if (!pool_codes.count(fina_codes[i])) continue;
        std::string e8 = end8(fina_end[i]);
        if (e8.empty()) continue;
        std::map<std::string, double> vals;
        vals["roe"] = to_double(fina_roe[i]);
        vals["debt_to_assets"] = to_double(fina_debt[i]);
        vals["ocf_to_profit"] = to_double(fina_ocf[i]);
        vals["netprofit_yoy"] = to_double(fina_yoy[i]);
        fina_map[fina_codes[i]][e8].push_back(vals);
    }

    // Helper to get latest value for a code/period
    auto get_latest = [&](const std::string& code, const std::string& period, const std::string& key) -> double {
        auto it = fina_map.find(code);
        if (it == fina_map.end()) return std::numeric_limits<double>::quiet_NaN();
        auto it2 = it->second.find(period);
        if (it2 == it->second.end() || it2->second.empty()) return std::numeric_limits<double>::quiet_NaN();
        const auto& row = it2->second.back();
        auto it3 = row.find(key);
        if (it3 == row.end()) return std::numeric_limits<double>::quiet_NaN();
        return it3->second;
    };

    // Verify Moutai
    fmt::print("\n  [数据校验] 贵州茅台各报告期数据：\n");
    for (const auto& p : target_periods) {
        double roe = get_latest("600519.SH", p, "roe");
        double debt = get_latest("600519.SH", p, "debt_to_assets");
        double ocf = get_latest("600519.SH", p, "ocf_to_profit");
        fmt::print("    {}: ROE={:.2f}, 负债率={:.2f}, 现金流/利润={:.2f}\n",
                   p, valid(roe) ? roe : 0.0, valid(debt) ? debt : 0.0, valid(ocf) ? ocf : 0.0);
    }

    // Step 5: ROE filter
    fmt::print("\n[Step 5] ROE>{}%（共{}期）...\n", cfg.roe_min, target_periods.size());
    std::set<std::string> roe_pass;
    for (const auto& code : pool_codes) {
        bool ok = true;
        for (const auto& p : target_periods) {
            double roe = get_latest(code, p, "roe");
            if (!valid(roe) || roe <= cfg.roe_min) { ok = false; break; }
        }
        if (ok) roe_pass.insert(code);
    }
    funnel.emplace_back(fmt::format("ROE>{}%", cfg.roe_min), static_cast<int>(roe_pass.size()));
    fmt::print("  {} 期都有 ROE 数据：{} 只\n", target_periods.size(), roe_pass.size());
    fmt::print("  通过 ROE 筛选：{} 只\n", roe_pass.size());

    if (roe_pass.empty()) {
        fmt::print("  没有股票通过，建议降低 ROE_MIN 或减少 ROE_CONSECUTIVE_YEARS\n");
        return 0;
    }

    // Step 6: debt filter
    fmt::print("\n[Step 6] 资产负债率 < {}%...\n", cfg.debt_to_assets_max);
    std::set<std::string> debt_pass;
    const std::string latest_period = target_periods.back();
    for (const auto& code : roe_pass) {
        double debt = get_latest(code, latest_period, "debt_to_assets");
        if (valid(debt) && debt < cfg.debt_to_assets_max) debt_pass.insert(code);
    }
    funnel.emplace_back(fmt::format("负债率<{}%", cfg.debt_to_assets_max), static_cast<int>(debt_pass.size()));
    fmt::print("  通过负债率筛选：{} 只\n", debt_pass.size());

    if (debt_pass.empty()) {
        fmt::print("  没有股票通过负债率筛选\n");
        return 0;
    }

    // Step 7: OCF or netprofit_yoy
    std::set<std::string> final_codes;
    bool has_ocf = false;
    for (const auto& code : debt_pass) {
        double ocf = get_latest(code, latest_period, "ocf_to_profit");
        if (valid(ocf)) { has_ocf = true; break; }
    }

    if (!has_ocf && cfg.use_netprofit_yoy_when_no_ocf) {
        fmt::print("\n[Step 7] 净利润同比增长 > {}%（当前无经营性现金流/净利润数据）...\n", cfg.netprofit_yoy_min);
        for (const auto& code : debt_pass) {
            double yoy = get_latest(code, latest_period, "netprofit_yoy");
            if (valid(yoy) && yoy > cfg.netprofit_yoy_min) final_codes.insert(code);
        }
        funnel.emplace_back(fmt::format("净利润同比>{}%", cfg.netprofit_yoy_min), static_cast<int>(final_codes.size()));
        fmt::print("  通过净利润同比筛选：{} 只\n", final_codes.size());
    } else if (has_ocf) {
        fmt::print("\n[Step 7] 经营性现金流/净利润 > {}...\n", cfg.ocf_to_profit_min);
        for (const auto& code : debt_pass) {
            double ocf = get_latest(code, latest_period, "ocf_to_profit");
            if (valid(ocf) && ocf > cfg.ocf_to_profit_min) final_codes.insert(code);
        }
        funnel.emplace_back("现金流/利润达标", static_cast<int>(final_codes.size()));
        fmt::print("  通过现金流/利润筛选：{} 只\n", final_codes.size());
    } else {
        final_codes = debt_pass;
        funnel.emplace_back("第3条件未筛", static_cast<int>(final_codes.size()));
        fmt::print("  未启用第 3 条件，通过 {} 只\n", final_codes.size());
    }

    // Print funnel
    fmt::print("\n{:=<70}\n", "");
    fmt::print("筛选漏斗：从全市场到优质公司\n");
    fmt::print("{:=<70}\n", "");
    for (size_t i = 0; i < funnel.size(); ++i) {
        if (i == 0) {
            fmt::print("  {:<30s}  {:>5d} 只\n", funnel[i].first, funnel[i].second);
        } else {
            int removed = funnel[i - 1].second - funnel[i].second;
            fmt::print("  -> {:<28s}  {:>5d} 只  (去掉 {})\n", funnel[i].first, funnel[i].second, removed);
        }
    }
    fmt::print("{:=<70}\n", "");

    if (final_codes.empty()) {
        fmt::print("\n没有符合所有条件的股票\n");
        fmt::print("建议：降低 ROE_MIN 或放宽 DEBT_TO_ASSETS_MAX\n");
        return 0;
    }

    // Build result table
    std::vector<std::string> ts_codes = stocks.column("ts_code");
    std::vector<std::string> names = stocks.column("name");
    std::vector<std::string> industries = stocks.column("industry");

    struct Result {
        std::string ts_code;
        std::string name;
        std::string industry;
        std::map<std::string, double> roe_by_year;
        double debt = 0.0;
        double ocf = 0.0;
        double yoy = 0.0;
        double latest_roe = 0.0;
    };

    std::vector<Result> results;
    for (size_t i = 0; i < ts_codes.size(); ++i) {
        if (!final_codes.count(ts_codes[i])) continue;
        Result r;
        r.ts_code = ts_codes[i];
        r.name = names[i];
        r.industry = industries[i];
        for (const auto& p : target_periods) {
            r.roe_by_year[p.substr(0, 4)] = get_latest(ts_codes[i], p, "roe");
        }
        r.debt = get_latest(ts_codes[i], latest_period, "debt_to_assets");
        r.ocf = get_latest(ts_codes[i], latest_period, "ocf_to_profit");
        r.yoy = get_latest(ts_codes[i], latest_period, "netprofit_yoy");
        r.latest_roe = get_latest(ts_codes[i], latest_period, "roe");
        results.push_back(r);
    }

    std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        return a.latest_roe > b.latest_roe;
    });

    // Print results
    fmt::print("\n最终入选（共 {} 只，按最新 ROE 从高到低）：\n", results.size());
    fmt::print("{:-<70}\n", "");
    fmt::print("{:<12} {:<10} {:<12}", "代码", "名称", "行业");
    for (const auto& p : target_periods) fmt::print("ROE{:<6}", p.substr(0, 4));
    fmt::print("负债率(%)  ");
    if (has_ocf) fmt::print("现金流/利润");
    else fmt::print("净利润同比(%)");
    fmt::print("\n");
    fmt::print("{:-<70}\n", "");

    size_t show_n = std::min<size_t>(50, results.size());
    for (size_t i = 0; i < show_n; ++i) {
        const auto& r = results[i];
        fmt::print("{:<12} {:<10} {:<12}", r.ts_code, r.name, r.industry);
        for (const auto& p : target_periods) {
            double roe = r.roe_by_year.at(p.substr(0, 4));
            fmt::print("{:<8.2f}  ", valid(roe) ? roe : 0.0);
        }
        fmt::print("{:<10.2f} ", valid(r.debt) ? r.debt : 0.0);
        if (has_ocf) fmt::print("{:<11.2f}", valid(r.ocf) ? r.ocf : 0.0);
        else fmt::print("{:<13.2f}", valid(r.yoy) ? r.yoy : 0.0);
        fmt::print("\n");
    }
    if (results.size() > show_n) {
        fmt::print("\n... 还有 {} 只，完整结果见CSV文件\n", results.size() - show_n);
    }

    // Industry distribution
    std::map<std::string, int> industry_counts;
    for (const auto& r : results) industry_counts[r.industry]++;
    std::vector<std::pair<std::string, int>> sorted_ind(industry_counts.begin(), industry_counts.end());
    std::sort(sorted_ind.begin(), sorted_ind.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    fmt::print("\n{:-<70}\n", "");
    fmt::print("行业分布（什么行业容易出\"好公司\"？）：\n");
    fmt::print("{:-<70}\n", "");
    if (!sorted_ind.empty()) {
        int max_count = sorted_ind.front().second;
        for (const auto& kv : sorted_ind) {
            int bar_len = static_cast<int>(kv.second * 30.0 / max_count);
            fmt::print("  {:<10s} {:>3d} 只  {}\n", kv.first, kv.second, std::string(bar_len, '#'));
        }
    }

    // ROE trend
    fmt::print("\n{:-<70}\n", "");
    fmt::print("入选股票 ROE 趋势（{}-{}年平均值）：\n", target_periods.front().substr(0, 4), target_periods.back().substr(0, 4));
    for (const auto& p : target_periods) {
        std::string year = p.substr(0, 4);
        double sum = 0.0;
        int n = 0;
        for (const auto& r : results) {
            double v = r.roe_by_year.at(year);
            if (valid(v)) { sum += v; n++; }
        }
        fmt::print("  {} 年平均 ROE：{:.2f}%\n", year, n ? (sum / n) : 0.0);
    }

    // Teaching points
    fmt::print("\n{:=<70}\n", "");
    fmt::print("教学要点\n");
    fmt::print("{:=<70}\n", "");
    fmt::print("  1. 从 {} 只 -> {} 只：这就是量化选股的\"漏斗\"思维\n", funnel.front().second, results.size());
    fmt::print("  2. 连续{}年 ROE>{}% 是巴菲特核心标准（他要求15%以上）\n", cfg.roe_consecutive_years, cfg.roe_min);
    fmt::print("  3. 排除金融股：银行/保险的高杠杆是行业特性，不能用<{}%标准衡量\n", cfg.debt_to_assets_max);
    fmt::print("  4. 现金流/利润>{:.0f}% 排除\"纸面利润\"：\n", cfg.ocf_to_profit_min * 100);
    fmt::print("     - 应收账款堆积（卖了货但没收到钱）\n");
    fmt::print("     - 关联交易虚增收入\n");
    fmt::print("     - 经典案例：某环保公司利润好看但现金流为负\n");
    fmt::print("  5. 筛出来 =/= 可以直接买！还需要看估值（PE/PB是否合理）\n");
    fmt::print("\n  进阶思考：\n");
    fmt::print("  - 把 ROE_MIN 改成 20%，还剩多少只？\n");
    fmt::print("  - 加条件：netprofit_yoy > 10%（净利润同比增长>10%）\n");
    fmt::print("  - 看看结果中有没有你熟悉的公司？\n");
    fmt::print("{:=<70}\n", "");

    // Save CSV
    fs::create_directories(DATA_DIR);
    std::vector<std::string> headers = {"ts_code", "name", "industry"};
    for (const auto& p : target_periods) headers.push_back("roe_" + p.substr(0, 4));
    headers.push_back("debt_to_assets");
    if (has_ocf) headers.push_back("ocf_to_profit");
    headers.push_back("netprofit_yoy");

    std::vector<std::vector<std::string>> rows;
    for (const auto& r : results) {
        std::vector<std::string> row = {r.ts_code, r.name, r.industry};
        for (const auto& p : target_periods) {
            double v = r.roe_by_year.at(p.substr(0, 4));
            row.push_back(valid(v) ? fmt::format("{:.2f}", v) : "");
        }
        row.push_back(valid(r.debt) ? fmt::format("{:.2f}", r.debt) : "");
        if (has_ocf) row.push_back(valid(r.ocf) ? fmt::format("{:.2f}", r.ocf) : "");
        row.push_back(valid(r.yoy) ? fmt::format("{:.2f}", r.yoy) : "");
        rows.push_back(row);
    }
    quant::csv::write_csv(DATA_DIR + "/11-综合基本面选股_result.csv", headers, rows);
    fmt::print("\n完整结果已保存：{}/11-综合基本面选股_result.csv\n", DATA_DIR);

    return 0;
}
