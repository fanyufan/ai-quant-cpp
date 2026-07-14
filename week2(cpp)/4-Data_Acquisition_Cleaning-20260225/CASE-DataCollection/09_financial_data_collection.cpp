// 对应 Python: 2-财务数据采集.py
// 实现方式：使用 Tushare fina_indicator 接口替代 MiniQMT，输出 CSV + SQL 文件（不写 MySQL）。
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <fmt/format.h>

#include "csv.hpp"
#include "tushare_client.hpp"

namespace fs = std::filesystem;

static std::string json_to_string(const nlohmann::json& j) {
    if (j.is_null()) return "";
    if (j.is_string()) return j.get<std::string>();
    return j.dump();
}

int main(int argc, char* argv[]) {
    const char* token_env = std::getenv("TUSHARE_TOKEN");
    if (!token_env || std::string(token_env).empty()) {
        fmt::print("错误：未找到环境变量 TUSHARE_TOKEN\n");
        return 1;
    }

    bool test_mode = true;
    std::string test_stock = "600519.SH";
    std::string output_dir = "data";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--full") {
            test_mode = false;
        } else if ((arg == "--test-stock") && i + 1 < argc) {
            test_stock = argv[++i];
        } else if ((arg == "--output-dir") && i + 1 < argc) {
            output_dir = argv[++i];
        }
    }

    std::string csv_path = fmt::format("{}/trade_stock_financial.csv", output_dir);
    std::string sql_path = fmt::format("{}/trade_stock_financial.sql", output_dir);

    fmt::print("财务数据采集 (Tushare -> CSV/SQL)\n");
    if (test_mode) {
        fmt::print("[测试模式] 只采集 {}\n", test_stock);
    } else {
        fmt::print("[全量模式] 采集沪深A股财务指标\n");
    }
    fmt::print("{:-<60}\n", "");

    try {
        quant::tushare::Client client(token_env);

        std::vector<std::string> codes;
        if (test_mode) {
            codes = {test_stock};
        } else {
            fmt::print("获取股票列表...\n");
            auto basic = client.stock_basic("", "L", "ts_code");
            if (!basic.contains("data") || basic["data"].empty()) {
                fmt::print("错误：无法获取股票列表\n");
                return 1;
            }
            auto fields = basic["data"]["fields"];
            auto items = basic["data"]["items"];
            int ts_idx = -1;
            for (size_t i = 0; i < fields.size(); ++i) {
                if (fields[i].template get<std::string>() == "ts_code") ts_idx = static_cast<int>(i);
            }
            for (const auto& item : items) {
                codes.push_back(item[ts_idx].get<std::string>());
            }
        }

        // 读取已有 CSV，跳过已存在的股票（断点续传）
        std::set<std::string> existing_stocks;
        std::vector<std::vector<std::string>> all_rows;
        std::vector<std::string> headers = {
            "stock_code", "report_date", "revenue", "net_profit", "eps",
            "roe", "roa", "gross_margin", "net_margin", "debt_ratio",
            "current_ratio", "operating_cashflow", "total_assets", "total_equity", "data_source"
        };
        if (fs::exists(csv_path)) {
            auto old = quant::csv::read_csv(csv_path);
            auto code_idx_opt = old.col_index("stock_code");
            if (code_idx_opt) {
                size_t ci = *code_idx_opt;
                for (const auto& row : old.rows) {
                    all_rows.push_back(row);
                    existing_stocks.insert(row[ci]);
                }
            }
        }

        const std::string fina_fields =
            "ts_code,end_date,roe,roa,grossprofit_margin,netprofit_margin,"
            "debt_to_assets,current_ratio,ocf_to_revenue,eps,bps";

        size_t total = codes.size();
        size_t success = 0;
        size_t skipped = 0;
        size_t new_rows = 0;

        for (size_t i = 0; i < codes.size(); ++i) {
            const std::string& code = codes[i];
            if (existing_stocks.count(code)) {
                ++skipped;
                continue;
            }

            try {
                auto res = client.fina_indicator(code, "", fina_fields);
                if (!res.contains("data") || res["data"].empty()) {
                    continue;
                }
                auto fields = res["data"]["fields"];
                auto items = res["data"]["items"];
                auto get_idx = [&](const std::string& name) -> int {
                    for (size_t k = 0; k < fields.size(); ++k) {
                        if (fields[k].template get<std::string>() == name) return static_cast<int>(k);
                    }
                    return -1;
                };
                int ed_idx = get_idx("end_date");
                int roe_idx = get_idx("roe");
                int roa_idx = get_idx("roa");
                int gm_idx = get_idx("grossprofit_margin");
                int nm_idx = get_idx("netprofit_margin");
                int dr_idx = get_idx("debt_to_assets");
                int cr_idx = get_idx("current_ratio");
                int ocf_idx = get_idx("ocf_to_revenue");
                int eps_idx = get_idx("eps");
                if (ed_idx < 0) continue;

                for (const auto& item : items) {
                    std::string end_date = json_to_string(item[ed_idx]);
                    if (end_date.size() != 8) continue;
                    std::string report_date = fmt::format("{}-{}-{}", end_date.substr(0, 4),
                                                          end_date.substr(4, 2), end_date.substr(6, 2));
                    auto get = [&](int idx) -> std::string {
                        if (idx < 0) return "";
                        return json_to_string(item[idx]);
                    };
                    std::vector<std::string> row = {
                        code, report_date, "", "", get(eps_idx),
                        get(roe_idx), get(roa_idx), get(gm_idx), get(nm_idx), get(dr_idx),
                        get(cr_idx), get(ocf_idx), "", "", "tushare"
                    };
                    all_rows.push_back(row);
                    ++new_rows;
                }
                existing_stocks.insert(code);
                ++success;
            } catch (const std::exception& e) {
                fmt::print("  {} 失败：{}\n", code, e.what());
            }

            if ((i + 1) % 10 == 0 || i + 1 == total) {
                fmt::print("\r  进度 {}/{} | 成功 {} | 跳过 {} | 新增 {} 条",
                           i + 1, total, success, skipped, new_rows);
            }
        }
        fmt::print("\n");

        fs::create_directories(output_dir);
        quant::csv::write_csv(csv_path, headers, all_rows);
        fmt::print("CSV 已保存：{} ({} 条)\n", csv_path, all_rows.size());

        std::ofstream sql_ofs(sql_path, std::ios::binary);
        sql_ofs.write("\xEF\xBB\xBF", 3);
        sql_ofs << "-- trade_stock_financial insert statements (generated by 09_financial_data_collection.cpp)\n";
        for (const auto& row : all_rows) {
            sql_ofs << "INSERT INTO trade_stock_financial "
                       "(stock_code, report_date, revenue, net_profit, eps, roe, roa, "
                       "gross_margin, net_margin, debt_ratio, current_ratio, operating_cashflow, "
                       "total_assets, total_equity, data_source) "
                       "VALUES ('" << row[0] << "', '" << row[1] << "', "
                    << (row[2].empty() ? "NULL" : row[2]) << ", "
                    << (row[3].empty() ? "NULL" : row[3]) << ", "
                    << (row[4].empty() ? "NULL" : "'" + row[4] + "'") << ", "
                    << (row[5].empty() ? "NULL" : row[5]) << ", "
                    << (row[6].empty() ? "NULL" : row[6]) << ", "
                    << (row[7].empty() ? "NULL" : row[7]) << ", "
                    << (row[8].empty() ? "NULL" : row[8]) << ", "
                    << (row[9].empty() ? "NULL" : row[9]) << ", "
                    << (row[10].empty() ? "NULL" : row[10]) << ", "
                    << (row[11].empty() ? "NULL" : row[11]) << ", "
                    << (row[12].empty() ? "NULL" : row[12]) << ", "
                    << (row[13].empty() ? "NULL" : row[13]) << ", "
                    << (row[14].empty() ? "NULL" : "'" + row[14] + "'") << ") "
                       "ON DUPLICATE KEY UPDATE "
                       "revenue=VALUES(revenue), net_profit=VALUES(net_profit), eps=VALUES(eps), "
                       "roe=VALUES(roe), roa=VALUES(roa), gross_margin=VALUES(gross_margin), "
                       "net_margin=VALUES(net_margin), debt_ratio=VALUES(debt_ratio), "
                       "current_ratio=VALUES(current_ratio), operating_cashflow=VALUES(operating_cashflow), "
                       "total_assets=VALUES(total_assets), total_equity=VALUES(total_equity), "
                       "data_source=VALUES(data_source);\n";
        }
        fmt::print("SQL 已保存：{}\n", sql_path);

        return 0;
    } catch (const std::exception& e) {
        fmt::print("运行错误：{}\n", e.what());
        return 1;
    }
}
