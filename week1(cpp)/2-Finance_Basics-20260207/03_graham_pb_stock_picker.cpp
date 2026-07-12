// 对应 Python: 3-格雷厄姆PB选股.py
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <cmath>
#include <limits>
#include <filesystem>
#include <fmt/format.h>
#include "csv.hpp"

namespace fs = std::filesystem;

struct Stock {
    std::string ts_code;
    std::string name;
    std::string industry;
    double close = 0.0;
    double pb = 0.0;
    double pe = 0.0;
    double total_mv = 0.0;
    double roe = 0.0;
};

int main() {
    const double PB_MAX = 1.0;
    const double ROE_MIN = 5.0;
    const std::string DATA_DIR = "data";

    std::string stock_file = DATA_DIR + "/stock_basic.csv";
    std::string daily_file = DATA_DIR + "/daily_basic_latest.csv";
    std::string fina_file = DATA_DIR + "/fina_indicator_pool.csv";

    std::vector<std::string> missing;
    if (!fs::exists(stock_file)) missing.push_back("stock_basic.csv（股票列表）");
    if (!fs::exists(daily_file)) missing.push_back("daily_basic_latest.csv（估值数据）");
    if (!fs::exists(fina_file)) missing.push_back("fina_indicator_pool.csv（财务指标）");

    if (!missing.empty()) {
        fmt::print("错误：缺少数据文件，请先运行数据下载脚本\n");
        fmt::print("缺少的文件：\n");
        for (const auto& m : missing) fmt::print("  {}\n", m);
        fmt::print("\n请执行：python 10-数据下载-tushare财务数据.py\n");
        return 1;
    }

    auto stock_csv = quant::csv::read_csv(stock_file);
    auto daily_csv = quant::csv::read_csv(daily_file);
    auto fina_csv = quant::csv::read_csv(fina_file);

    // Get latest report period
    std::string period;
    auto fina_dates = fina_csv.column("end_date");
    for (const auto& d : fina_dates) {
        std::string cleaned;
        for (char c : d) if (std::isdigit(c)) cleaned += c;
        if (cleaned.size() == 8) {
            if (period.empty() || cleaned > period) period = cleaned;
        }
    }
    if (period.empty()) period = "20241231";
    std::string roe_year = period.substr(0, 4);

    // Get trade date
    std::string trade_date;
    auto td_col = daily_csv.column("trade_date");
    if (!td_col.empty()) {
        std::string td = td_col[0];
        for (char c : td) if (std::isdigit(c)) trade_date += c;
        if (trade_date.size() == 8) {
            trade_date = fmt::format("{}-{}-{}", trade_date.substr(0, 4),
                                     trade_date.substr(4, 2), trade_date.substr(6, 2));
        }
    }

    fmt::print("{:=<70}\n", "");
    fmt::print("筛选条件：PB < {}（破净）且 ROE > {}%（仍盈利）\n", PB_MAX, ROE_MIN);
    fmt::print("估值日期：{}    ROE来源：{}年报告期 {}\n", trade_date, roe_year, period);
    fmt::print("{:=<70}\n", "");

    // Build stock map excluding ST
    std::map<std::string, Stock> stock_map;
    auto ts_codes = stock_csv.column("ts_code");
    auto names = stock_csv.column("name");
    auto industries = stock_csv.column("industry");

    for (size_t i = 0; i < ts_codes.size(); ++i) {
        if (names[i].find("ST") != std::string::npos ||
            names[i].find("st") != std::string::npos) {
            continue;
        }
        Stock s;
        s.ts_code = ts_codes[i];
        s.name = names[i];
        s.industry = (i < industries.size()) ? industries[i] : "";
        stock_map[s.ts_code] = s;
    }

    size_t total_stocks = ts_codes.size();
    size_t st_count = 0;
    for (const auto& name : names) {
        if (name.find("ST") != std::string::npos || name.find("st") != std::string::npos) {
            ++st_count;
        }
    }
    fmt::print("\n全市场 {} 只，排除 {} 只ST股，剩余 {} 只\n",
               total_stocks, st_count, total_stocks - st_count);

    // Merge daily
    auto d_ts = daily_csv.column("ts_code");
    auto d_close = daily_csv.column_double("close");
    auto d_pb = daily_csv.column_double("pb");
    auto d_pe = daily_csv.column_double("pe");
    auto d_mv = daily_csv.column_double("total_mv");

    size_t with_pb = 0;
    for (size_t i = 0; i < d_ts.size(); ++i) {
        auto it = stock_map.find(d_ts[i]);
        if (it == stock_map.end()) continue;
        if (i < d_close.size()) it->second.close = d_close[i];
        if (i < d_pb.size()) it->second.pb = d_pb[i];
        if (i < d_pe.size()) it->second.pe = d_pe[i];
        if (i < d_mv.size()) it->second.total_mv = d_mv[i];
        if (!std::isnan(it->second.pb)) ++with_pb;
    }
    fmt::print("有 PB 数据的：{} 只\n", with_pb);

    // Merge fina ROE for latest period
    auto f_ts = fina_csv.column("ts_code");
    auto f_end = fina_csv.column("end_date");
    auto f_roe = fina_csv.column_double("roe");

    std::map<std::string, double> latest_roe;
    for (size_t i = 0; i < f_ts.size(); ++i) {
        std::string cleaned;
        for (char c : f_end[i]) if (std::isdigit(c)) cleaned += c;
        if (cleaned != period) continue;
        latest_roe[f_ts[i]] = (i < f_roe.size()) ? f_roe[i] : std::numeric_limits<double>::quiet_NaN();
    }

    size_t with_both = 0;
    for (auto& [code, s] : stock_map) {
        auto it = latest_roe.find(code);
        if (it != latest_roe.end()) {
            s.roe = it->second;
            if (!std::isnan(s.pb) && !std::isnan(s.roe)) ++with_both;
        }
    }
    fmt::print("有 PB + ROE 数据的：{} 只\n", with_both);

    // Filter
    std::vector<Stock> final;
    size_t pb_candidates = 0;
    for (const auto& [code, s] : stock_map) {
        if (std::isnan(s.pb) || std::isnan(s.roe)) continue;
        if (s.pb > 0 && s.pb < PB_MAX) ++pb_candidates;
        if (s.pb > 0 && s.pb < PB_MAX && s.roe > ROE_MIN) {
            final.push_back(s);
        }
    }

    std::sort(final.begin(), final.end(), [](const Stock& a, const Stock& b) {
        return a.pb < b.pb;
    });

    fmt::print("\n{:=<70}\n", "");
    fmt::print("筛选结果\n");
    fmt::print("{:=<70}\n", "");
    fmt::print("  破净候选（PB<{}）：{} 只\n", PB_MAX, pb_candidates);
    fmt::print("  加 ROE>{}% 后：{} 只\n", ROE_MIN, final.size());
    fmt::print("{:-<70}\n", "");

    if (final.empty()) {
        fmt::print("没有同时满足条件的股票，建议放宽 PB_MAX 或降低 ROE_MIN\n");
        return 0;
    }

    size_t show_n = std::min<size_t>(30, final.size());
    fmt::print("\n前 {} 只（按 PB 从低到高）：\n", show_n);
    fmt::print("{:-<70}\n", "");
    fmt::print("{:<12} {:<10} {:<12} {:>8} {:>8} {:>10} {:>10}\n",
               "代码", "名称", "行业", "收盘价", "PB", "ROE(%)", "市值(亿)");
    for (size_t i = 0; i < show_n; ++i) {
        const auto& s = final[i];
        fmt::print("{:<12} {:<10} {:<12} {:>8.2f} {:>8.3f} {:>10.2f} {:>10.1f}\n",
                   s.ts_code, s.name, s.industry, s.close, s.pb, s.roe, s.total_mv / 10000.0);
    }
    if (final.size() > show_n) {
        fmt::print("\n... 还有 {} 只，完整结果见CSV文件\n", final.size() - show_n);
    }

    // Industry distribution
    std::map<std::string, int> ind_count;
    int max_count = 0;
    for (const auto& s : final) {
        int c = ++ind_count[s.industry];
        if (c > max_count) max_count = c;
    }

    fmt::print("\n{:-<70}\n", "");
    fmt::print("行业分布（破净股集中在哪些行业？）：\n");
    fmt::print("{:-<70}\n", "");
    std::vector<std::pair<std::string, int>> sorted_ind(ind_count.begin(), ind_count.end());
    std::sort(sorted_ind.begin(), sorted_ind.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    for (size_t i = 0; i < std::min<size_t>(15, sorted_ind.size()); ++i) {
        const auto& [ind_name, count] = sorted_ind[i];
        int bar_len = max_count > 0 ? static_cast<int>(count * 30 / max_count) : 0;
        fmt::print("  {:<10} {:>3} 只  {}\n", ind_name, count, std::string(bar_len, '#'));
    }

    // PB distribution
    double min_pb = final.front().pb;
    double max_pb = final.back().pb;
    double sum_pb = 0.0;
    for (const auto& s : final) sum_pb += s.pb;
    double avg_pb = sum_pb / final.size();
    std::vector<double> pbs;
    for (const auto& s : final) pbs.push_back(s.pb);
    std::sort(pbs.begin(), pbs.end());
    double median_pb = pbs[pbs.size() / 2];

    fmt::print("\n{:-<70}\n", "");
    fmt::print("PB 分布：\n");
    fmt::print("  最低：{:.3f}（{}）\n", min_pb, final.front().name);
    fmt::print("  最高：{:.3f}\n", max_pb);
    fmt::print("  平均：{:.3f}\n", avg_pb);
    fmt::print("  中位：{:.3f}\n", median_pb);

    // Save results
    std::vector<std::string> headers = {"ts_code", "name", "industry", "close", "pb", "roe", "pe", "total_mv"};
    std::vector<std::vector<std::string>> rows;
    for (const auto& s : final) {
        rows.push_back({s.ts_code, s.name, s.industry,
                        fmt::format("{:.2f}", s.close),
                        fmt::format("{:.3f}", s.pb),
                        fmt::format("{:.2f}", s.roe),
                        fmt::format("{:.2f}", s.pe),
                        fmt::format("{:.2f}", s.total_mv)});
    }
    std::string out_path = DATA_DIR + "/10-格雷厄姆PB选股_result.csv";
    quant::csv::write_csv(out_path, headers, rows);
    fmt::print("\n完整结果已保存：{}\n", out_path);

    return 0;
}
