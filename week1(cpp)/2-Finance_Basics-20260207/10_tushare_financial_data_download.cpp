// 对应 Python: 数据下载-tushare财务数据.py
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include "csv.hpp"
#include "tushare_client.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string get_token() {
    const char* env = std::getenv("TUSHARE_TOKEN");
    if (!env || std::string(env).empty()) {
        fmt::print("错误：未设置环境变量 TUSHARE_TOKEN\n");
        std::exit(1);
    }
    return env;
}

std::string get_latest_report_period() {
    auto now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    tm local;
#ifdef _WIN32
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    int y = local.tm_year + 1900;
    int m = local.tm_mon + 1;
    if (m >= 10) return fmt::format("{}0930", y);
    if (m >= 8) return fmt::format("{}0630", y);
    if (m >= 4) return fmt::format("{}0331", y);
    return fmt::format("{}0930", y - 1);
}

std::vector<std::string> get_report_periods_annual(int n) {
    auto now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    tm local;
#ifdef _WIN32
    localtime_s(&local, &tt);
#else
    localtime_r(&tt, &local);
#endif
    int y = local.tm_year + 1900;
    if (local.tm_mon + 1 < 4) y -= 1;
    std::vector<std::string> out;
    for (int i = 0; i < n; ++i) out.push_back(fmt::format("{}1231", y - i));
    return out;
}

std::string clean_date(const std::string& s) {
    std::string out;
    for (char c : s) if (std::isdigit(c)) out += c;
    if (out.size() >= 8) return out.substr(0, 8);
    return out;
}

void write_stock_basic(const json& data, const std::string& path) {
    auto fields = data.value("fields", json::array());
    auto items = data.value("items", json::array());

    std::vector<std::string> headers = {"ts_code", "name", "industry", "symbol", "market"};
    std::vector<std::vector<std::string>> rows;
    for (const auto& item : items) {
        std::string ts_code = item[0].get<std::string>();
        auto ends_with = [](const std::string& s, const std::string& suffix) {
            return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        if (!(ends_with(ts_code, ".SH") || ends_with(ts_code, ".SZ"))) continue;
        std::string name = item[1].get<std::string>();
        std::string industry = item.size() > 2 && !item[2].is_null() ? item[2].get<std::string>() : "";
        std::string symbol = ts_code.substr(0, ts_code.find('.'));
        std::string market = ts_code.substr(ts_code.find('.') + 1);
        rows.push_back({ts_code, name, industry, symbol, market});
    }
    quant::csv::write_csv(path, headers, rows);
    fmt::print("  已保存 {} 只股票 -> {}\n", rows.size(), path);
}

void merge_and_save_fina(const std::string& path,
                         const std::vector<std::map<std::string, std::string>>& new_records) {
    if (new_records.empty()) return;

    std::set<std::pair<std::string, std::string>> keys;
    for (const auto& r : new_records) {
        keys.insert({r.at("ts_code"), clean_date(r.at("end_date"))});
    }

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> headers = {"ts_code", "end_date", "roe", "bps", "eps",
                                        "debt_to_assets", "ocf_to_profit", "netprofit_yoy"};

    if (fs::exists(path)) {
        auto old = quant::csv::read_csv(path);
        auto old_ts = old.column("ts_code");
        auto old_end = old.column("end_date");
        for (size_t i = 0; i < old_ts.size(); ++i) {
            auto key = std::make_pair(old_ts[i], clean_date(old_end[i]));
            if (keys.count(key)) continue;
            std::vector<std::string> row;
            for (const auto& h : headers) row.push_back(old.get(i, h));
            rows.push_back(row);
        }
    }

    for (const auto& r : new_records) {
        std::vector<std::string> row;
        for (const auto& h : headers) {
            auto it = r.find(h);
            row.push_back(it != r.end() ? it->second : "");
        }
        rows.push_back(row);
    }

    quant::csv::write_csv(path, headers, rows);
}

int main() {
    const std::string DATA_DIR = "data";
    const std::string TRADE_DATE = "20260206";
    const bool USE_MULTIPLE_PERIODS = true;
    const int NUM_ANNUAL_PERIODS = 3;

    std::vector<std::string> report_periods;
    std::string report_label;
    if (USE_MULTIPLE_PERIODS) {
        report_periods = get_report_periods_annual(NUM_ANNUAL_PERIODS);
        std::string joined;
        for (size_t i = 0; i < report_periods.size(); ++i) {
            if (i > 0) joined += ",";
            joined += report_periods[i];
        }
        report_label = fmt::format("{} (多期年报)", joined);
    } else {
        report_periods = {get_latest_report_period()};
        report_label = fmt::format("{} (最近报告期)", report_periods[0]);
    }

    fmt::print("{:=<60}\n", "");
    fmt::print("基本面选股 -- 数据下载（Tushare 版）\n");
    fmt::print("{:=<60}\n", "");
    fmt::print("保存目录: {}\n", DATA_DIR);
    fmt::print("基准交易日: {}  报告期: {}\n", TRADE_DATE, report_label);
    fmt::print("{:=<60}\n", "");

    fs::create_directories(DATA_DIR);
    auto token = get_token();
    quant::tushare::Client client(token);

    // Step 1: stock_basic
    fmt::print("\n[Step 1/3] 获取 A 股股票列表...\n");
    json stock_basic_data;
    try {
        stock_basic_data = client.stock_basic("", "L", "ts_code,name,industry,market");
    } catch (const std::exception& e) {
        fmt::print("  请求失败: {}\n", e.what());
        return 1;
    }
    write_stock_basic(stock_basic_data, DATA_DIR + "/stock_basic.csv");

    std::vector<std::string> stock_list;
    for (const auto& item : stock_basic_data.value("items", json::array())) {
        std::string ts_code = item[0].get<std::string>();
        auto ends_with = [](const std::string& s, const std::string& suffix) {
            return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        if (ends_with(ts_code, ".SH") || ends_with(ts_code, ".SZ")) {
            stock_list.push_back(ts_code);
        }
    }

    // Step 2: fina_indicator
    fmt::print("\n[Step 2/3] 拉取财务指标（报告期 {}）...\n", report_label);
    std::string fina_path = DATA_DIR + "/fina_indicator_pool.csv";
    std::set<std::pair<std::string, std::string>> existing_keys;
    if (fs::exists(fina_path)) {
        auto old = quant::csv::read_csv(fina_path);
        auto old_ts = old.column("ts_code");
        auto old_end = old.column("end_date");
        for (size_t i = 0; i < old_ts.size(); ++i) {
            existing_keys.insert({old_ts[i], clean_date(old_end[i])});
        }
        fmt::print("  已存在 {} 条记录，仅拉取缺失的...\n", existing_keys.size());
    }

    std::vector<std::pair<std::string, std::string>> to_fetch;
    for (const auto& period : report_periods) {
        std::string p8 = clean_date(period);
        for (const auto& ts_code : stock_list) {
            if (!existing_keys.count({ts_code, p8})) {
                to_fetch.emplace_back(ts_code, period);
            }
        }
    }

    if (to_fetch.empty()) {
        fmt::print("  全部已存在，跳过 API 请求\n");
    } else {
        fmt::print("  需请求 {} 条...\n", to_fetch.size());
        const size_t BATCH_SIZE = 500;
        size_t total_ok = 0, total_fail = 0;
        size_t num_batches = (to_fetch.size() + BATCH_SIZE - 1) / BATCH_SIZE;

        for (size_t b = 0; b < num_batches; ++b) {
            std::vector<std::map<std::string, std::string>> batch_records;
            size_t start = b * BATCH_SIZE;
            size_t end = std::min(start + BATCH_SIZE, to_fetch.size());

            for (size_t i = start; i < end; ++i) {
                const auto& [ts_code, period] = to_fetch[i];
                try {
                    auto resp = client.fina_indicator(ts_code, period);
                    auto items = resp.value("items", json::array());
                    if (!items.empty()) {
                        const auto& item = items[0];
                        std::map<std::string, std::string> rec;
                        rec["ts_code"] = ts_code;
                        rec["end_date"] = clean_date(item[1].is_null() ? period : item[1].get<std::string>());
                        rec["roe"] = item.size() > 2 && !item[2].is_null() ? fmt::format("{}", item[2].get<double>()) : "";
                        rec["bps"] = item.size() > 3 && !item[3].is_null() ? fmt::format("{}", item[3].get<double>()) : "";
                        rec["eps"] = item.size() > 4 && !item[4].is_null() ? fmt::format("{}", item[4].get<double>()) : "";
                        rec["debt_to_assets"] = item.size() > 5 && !item[5].is_null() ? fmt::format("{}", item[5].get<double>()) : "";
                        rec["ocf_to_profit"] = "";
                        rec["netprofit_yoy"] = item.size() > 6 && !item[6].is_null() ? fmt::format("{}", item[6].get<double>()) : "";
                        batch_records.push_back(rec);
                    }
                } catch (const std::exception& e) {
                    // Most failures are "no data for this period", suppress per-item noise
                    if (i == start) fmt::print("  [API] 接口报错: {}...\n", e.what());
                }

                if ((i - start + 1) % 100 == 0 || i + 1 == end) {
                    fmt::print("  批 {}/{} 进度 {}/{}...\n", b + 1, num_batches, i - start + 1, end - start);
                }
            }

            merge_and_save_fina(fina_path, batch_records);
            total_ok += batch_records.size();
            total_fail += (end - start) - batch_records.size();
            fmt::print("  批 {}/{} 已合并保存，本批成功 {} 失败 {}\n", b + 1, num_batches, batch_records.size(), (end - start) - batch_records.size());
        }
        fmt::print("  合计: 成功 {}，失败 {}\n", total_ok, total_fail);
    }

    // Step 2b: ocf_to_profit (best-effort via VIP endpoints)
    fmt::print("\n[Step 2b] 尝试获取经营现金流/净利润（ocf_to_profit）...\n");
    bool ocf_filled = false;
    try {
        auto old = quant::csv::read_csv(fina_path);
        auto end_dates = old.column("end_date");
        std::set<std::string> periods;
        for (const auto& s : end_dates) periods.insert(clean_date(s));

        std::map<std::pair<std::string, std::string>, double> ocf_map;
        for (const auto& period : periods) {
            try {
                auto cf = client.cashflow_vip(period);
                auto inc = client.income_vip(period);
                auto cf_items = cf.value("items", json::array());
                auto inc_items = inc.value("items", json::array());

                std::map<std::string, double> ncf_map;
                std::map<std::string, double> ni_map;

                for (const auto& item : cf_items) {
                    if (item.size() < 4 || item[3].is_null() || item[3].get<int>() != 1) continue;
                    std::string code = item[0].get<std::string>();
                    double ncf = item[2].is_null() ? 0.0 : item[2].get<double>();
                    ncf_map[code] = ncf;
                }
                for (const auto& item : inc_items) {
                    if (item.size() < 5 || item[3].is_null() || item[3].get<int>() != 1) continue;
                    std::string code = item[0].get<std::string>();
                    double ni = 0.0;
                    if (!item[4].is_null()) ni = item[4].get<double>();
                    else if (item.size() > 4 && !item[2].is_null()) ni = item[2].get<double>();
                    if (ni != 0.0) ni_map[code] = ni;
                }

                for (const auto& [code, ncf] : ncf_map) {
                    auto it = ni_map.find(code);
                    if (it != ni_map.end() && it->second != 0.0) {
                        ocf_map[{code, period}] = ncf / it->second;
                    }
                }
            } catch (...) {}
        }

        if (!ocf_map.empty()) {
            auto headers = old.headers;
            if (std::find(headers.begin(), headers.end(), "ocf_to_profit") == headers.end()) {
                headers.push_back("ocf_to_profit");
            }
            std::vector<std::vector<std::string>> rows;
            auto ts_codes = old.column("ts_code");
            auto ends = old.column("end_date");
            for (size_t i = 0; i < ts_codes.size(); ++i) {
                std::vector<std::string> row;
                for (const auto& h : headers) {
                    if (h == "ocf_to_profit") {
                        auto it = ocf_map.find({ts_codes[i], clean_date(ends[i])});
                        row.push_back(it != ocf_map.end() ? fmt::format("{:.6f}", it->second) : "");
                    } else {
                        row.push_back(old.get(i, h));
                    }
                }
                rows.push_back(row);
            }
            quant::csv::write_csv(fina_path, headers, rows);
            fmt::print("  已填充 ocf_to_profit：{} 条\n", ocf_map.size());
            ocf_filled = true;
        }
    } catch (const std::exception& e) {
        fmt::print("  [Step 2b] 失败: {}\n", e.what());
    }
    if (!ocf_filled) {
        fmt::print("  未获取到现金流/利润数据（需 VIP 权限），ocf_to_profit 保持为空\n");
    }

    // Step 3: daily_basic
    fmt::print("\n[Step 3/3] 获取 {} 行情与估值...\n", TRADE_DATE);
    json daily_basic_data;
    try {
        daily_basic_data = client.daily_basic(TRADE_DATE, "ts_code,trade_date,close,pe,pb,total_mv");
    } catch (const std::exception& e) {
        fmt::print("  请求失败: {}\n", e.what());
        return 1;
    }

    std::set<std::string> stock_set(stock_list.begin(), stock_list.end());
    std::vector<std::string> headers = {"ts_code", "trade_date", "close", "pb", "pe", "total_mv"};
    std::vector<std::vector<std::string>> rows;

    std::map<std::string, std::pair<double, double>> bps_eps;
    {
        auto fina = quant::csv::read_csv(fina_path);
        auto ts_codes = fina.column("ts_code");
        auto ends = fina.column("end_date");
        auto bps_col = fina.column_double("bps");
        auto eps_col = fina.column_double("eps");
        std::map<std::string, std::tuple<std::string, double, double>> latest;
        for (size_t i = 0; i < ts_codes.size(); ++i) {
            auto key = ts_codes[i];
            if (!latest.count(key) || clean_date(ends[i]) > std::get<0>(latest[key])) {
                latest[key] = {clean_date(ends[i]), bps_col[i], eps_col[i]};
            }
        }
        for (const auto& kv : latest) {
            bps_eps[kv.first] = {std::get<1>(kv.second), std::get<2>(kv.second)};
        }
    }

    for (const auto& item : daily_basic_data.value("items", json::array())) {
        if (item.size() < 6) continue;
        std::string ts_code = item[0].get<std::string>();
        if (!stock_set.count(ts_code)) continue;
        std::string tdate = clean_date(item[1].is_null() ? TRADE_DATE : item[1].get<std::string>());
        double close = item[2].is_null() ? 0.0 : item[2].get<double>();
        double pe = item[3].is_null() ? 0.0 : item[3].get<double>();
        double pb = item[4].is_null() ? 0.0 : item[4].get<double>();
        double total_mv = item[5].is_null() ? 0.0 : item[5].get<double>();

        auto it = bps_eps.find(ts_code);
        if (it != bps_eps.end() && it->second.first > 0) {
            pb = close / it->second.first;
        }
        if (it != bps_eps.end() && it->second.second > 0) {
            pe = close / it->second.second;
        }

        rows.push_back({ts_code, tdate,
                        fmt::format("{:.2f}", close),
                        fmt::format("{:.3f}", pb),
                        fmt::format("{:.2f}", pe),
                        fmt::format("{:.2f}", total_mv)});
    }

    quant::csv::write_csv(DATA_DIR + "/daily_basic_latest.csv", headers, rows);
    fmt::print("  已保存 {} 只 -> {}\n", rows.size(), DATA_DIR + "/daily_basic_latest.csv");

    // Validate Moutai
    {
        auto fina = quant::csv::read_csv(fina_path);
        auto ts_codes = fina.column("ts_code");
        fmt::print("\n  [数据校验] 贵州茅台(600519.SH)：\n");
        bool found = false;
        for (size_t i = 0; i < ts_codes.size(); ++i) {
            if (ts_codes[i] == "600519.SH") {
                fmt::print("    {}: roe={}, debt_to_assets={}, bps={}\n",
                           fina.get(i, "end_date"), fina.get(i, "roe"),
                           fina.get(i, "debt_to_assets"), fina.get(i, "bps"));
                found = true;
            }
        }
        if (!found) fmt::print("    无数据\n");
    }

    fmt::print("\n{:=<60}\n", "");
    fmt::print("数据下载完成\n");
    fmt::print("{:=<60}\n", "");
    for (const auto& fname : {"stock_basic.csv", "daily_basic_latest.csv", "fina_indicator_pool.csv"}) {
        auto p = DATA_DIR + "/" + fname;
        if (fs::exists(p)) {
            fmt::print("  {:<30s}  {:.0f} KB\n", fname, static_cast<double>(fs::file_size(p)) / 1024.0);
        }
    }
    fmt::print("{:=<60}\n", "");

    return 0;
}
