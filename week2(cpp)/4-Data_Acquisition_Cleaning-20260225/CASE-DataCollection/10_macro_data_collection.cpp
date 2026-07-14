// 对应 Python: 8-宏观经济数据采集.py
// 实现方式：使用 Tushare 宏观接口（cn_gdp / cn_cpi / cn_ppi / cn_pmi / cn_m / sf_month / lpr_data）
// 输出 CSV + SQL 文件（不写 MySQL）。
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <fmt/format.h>

#include "csv.hpp"
#include "tushare_client.hpp"

namespace fs = std::filesystem;

struct MacroSource {
    std::string name;
    std::string period_key; // "month" or "date"
    std::vector<std::string> metrics;
};

static std::string today_yyyymm() {
    std::time_t now = std::time(nullptr);
    std::tm* lt = std::localtime(&now);
    return fmt::format("{:04d}{:02d}", lt->tm_year + 1900, lt->tm_mon + 1);
}

static std::string today_yyyymmdd() {
    std::time_t now = std::time(nullptr);
    std::tm* lt = std::localtime(&now);
    return fmt::format("{:04d}{:02d}{:02d}", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
}

static std::string today_yyyyq() {
    std::time_t now = std::time(nullptr);
    std::tm* lt = std::localtime(&now);
    int q = (lt->tm_mon / 3) + 1;
    return fmt::format("{:04d}Q{}", lt->tm_year + 1900, q);
}

static std::string format_period(const std::string& raw) {
    if (raw.size() == 6) {
        return fmt::format("{}-{}", raw.substr(0, 4), raw.substr(4, 2));
    }
    if (raw.size() == 8) {
        return fmt::format("{}-{}-{}", raw.substr(0, 4), raw.substr(4, 2), raw.substr(6, 2));
    }
    return raw;
}

static bool is_numeric(const std::string& s) {
    if (s.empty()) return false;
    bool dot_seen = false;
    size_t i = 0;
    if (s[0] == '-' || s[0] == '+') i = 1;
    for (; i < s.size(); ++i) {
        if (s[i] == '.') {
            if (dot_seen) return false;
            dot_seen = true;
        } else if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

static std::string json_to_string(const nlohmann::json& j) {
    if (j.is_null()) return "";
    if (j.is_string()) return j.get<std::string>();
    return j.dump();
}

static std::string join_strings(const std::vector<std::string>& parts, const std::string& sep) {
    std::string s;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) s += sep;
        s += parts[i];
    }
    return s;
}

int main(int argc, char* argv[]) {
    const char* token_env = std::getenv("TUSHARE_TOKEN");
    if (!token_env || std::string(token_env).empty()) {
        fmt::print("错误：未找到环境变量 TUSHARE_TOKEN\n");
        return 1;
    }

    std::string start_m = "201901";
    std::string end_m = today_yyyymm();
    std::string start_q = "2019Q1";
    std::string end_q = today_yyyyq();
    std::string start_date = "2019-01-01";
    std::string end_date = today_yyyymmdd();
    std::string output_dir = "data";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--start-m") && i + 1 < argc) start_m = argv[++i];
        else if ((arg == "--end-m") && i + 1 < argc) end_m = argv[++i];
        else if ((arg == "--output-dir") && i + 1 < argc) output_dir = argv[++i];
    }

    std::string csv_path = fmt::format("{}/macro_data.csv", output_dir);
    std::string sql_path = fmt::format("{}/macro_data.sql", output_dir);

    fmt::print("宏观经济数据采集 (Tushare -> CSV/SQL)\n");
    fmt::print("{:-<60}\n", "");

    try {
        quant::tushare::Client client(token_env);

        // 指标配置：接口名、周期字段、关心的指标
        std::vector<MacroSource> sources = {
            {"cn_gdp", "quarter", {"gdp_yoy"}},
            {"cn_cpi", "month", {"nt_yoy", "nt_mom"}},
            {"cn_ppi", "month", {"ppi_yoy", "ppi_mom"}},
            {"cn_pmi", "month", {"pmi010000", "pmi030000"}},
            {"cn_m", "month", {"m2_yoy", "m1_yoy"}},
            {"sf_month", "month", {"inc_month"}},
            {"lpr_data", "date", {"1y", "5y"}}
        };

        std::vector<std::string> headers = {"indicator", "period", "metric", "value"};
        std::vector<std::vector<std::string>> all_rows;

        for (const auto& src : sources) {
            fmt::print("正在获取 {} ... ", src.name);
            nlohmann::json res;
            try {
                if (src.name == "cn_gdp") {
                    res = client.cn_gdp(start_q, end_q, fmt::format("quarter,{}", join_strings(src.metrics, ",")));
                } else if (src.name == "cn_cpi") {
                    res = client.cn_cpi(start_m, end_m, fmt::format("month,{}", join_strings(src.metrics, ",")));
                } else if (src.name == "cn_ppi") {
                    res = client.cn_ppi(start_m, end_m, fmt::format("month,{}", join_strings(src.metrics, ",")));
                } else if (src.name == "cn_pmi") {
                    res = client.cn_pmi(start_m, end_m, fmt::format("month,{}", join_strings(src.metrics, ",")));
                } else if (src.name == "cn_m") {
                    res = client.cn_m(start_m, end_m, fmt::format("month,{}", join_strings(src.metrics, ",")));
                } else if (src.name == "sf_month") {
                    res = client.sf_month(start_m, end_m, fmt::format("month,{}", join_strings(src.metrics, ",")));
                } else if (src.name == "lpr_data") {
                    res = client.lpr_data(start_date, end_date, fmt::format("date,{}", join_strings(src.metrics, ",")));
                }
            } catch (const std::exception& e) {
                fmt::print("失败：{}\n", e.what());
                continue;
            }

            if (!res.contains("data") || res["data"].empty()) {
                fmt::print("无数据\n");
                continue;
            }
            auto fields = res["data"]["fields"];
            auto items = res["data"]["items"];

            auto field_idx = [&](const std::string& name) -> int {
                for (size_t i = 0; i < fields.size(); ++i) {
                    if (fields[i].template get<std::string>() == name) return static_cast<int>(i);
                }
                return -1;
            };

            int period_idx = field_idx(src.period_key);
            std::vector<int> metric_idxs;
            for (const auto& m : src.metrics) {
                int idx = field_idx(m);
                if (idx >= 0) metric_idxs.push_back(idx);
            }

            size_t count = 0;
            for (const auto& item : items) {
                if (period_idx < 0) continue;
                std::string period = format_period(json_to_string(item[period_idx]));
                for (size_t mi = 0; mi < src.metrics.size(); ++mi) {
                    if (metric_idxs[mi] < 0) continue;
                    std::string val = json_to_string(item[metric_idxs[mi]]);
                    if (val.empty() || !is_numeric(val)) continue;
                    all_rows.push_back({src.name, period, src.metrics[mi], val});
                    ++count;
                }
            }
            fmt::print("{} 条\n", count);
        }

        fs::create_directories(output_dir);
        quant::csv::write_csv(csv_path, headers, all_rows);
        fmt::print("\nCSV 已保存：{} ({} 条)\n", csv_path, all_rows.size());

        std::ofstream sql_ofs(sql_path, std::ios::binary);
        sql_ofs.write("\xEF\xBB\xBF", 3);
        sql_ofs << "-- macro_data insert statements (generated by 10_macro_data_collection.cpp)\n";
        sql_ofs << "CREATE TABLE IF NOT EXISTS macro_data ("
                   "indicator VARCHAR(32), period VARCHAR(16), metric VARCHAR(32), value DOUBLE, "
                   "PRIMARY KEY (indicator, period, metric));\n";
        for (const auto& row : all_rows) {
            sql_ofs << "INSERT INTO macro_data (indicator, period, metric, value) "
                       "VALUES ('" << row[0] << "', '" << row[1] << "', '" << row[2] << "', "
                    << row[3] << ") "
                       "ON DUPLICATE KEY UPDATE value=VALUES(value);\n";
        }
        fmt::print("SQL 已保存：{}\n", sql_path);

        // 打印最新值摘要
        fmt::print("\n最新宏观指标摘要：\n");
        auto key_metrics = std::set<std::tuple<std::string, std::string>>{
            {"cn_gdp", "gdp_yoy"}, {"cn_cpi", "nt_yoy"}, {"cn_ppi", "ppi_yoy"},
            {"cn_pmi", "pmi010000"}, {"cn_m", "m2_yoy"}, {"lpr_data", "1y"}
        };
        for (auto it = all_rows.rbegin(); it != all_rows.rend(); ++it) {
            auto key = std::make_tuple((*it)[0], (*it)[2]);
            if (key_metrics.count(key)) {
                fmt::print("  {}.{} = {} ({})\n", (*it)[0], (*it)[2], (*it)[3], (*it)[1]);
                key_metrics.erase(key);
            }
        }

        return 0;
    } catch (const std::exception& e) {
        fmt::print("运行错误：{}\n", e.what());
        return 1;
    }
}
