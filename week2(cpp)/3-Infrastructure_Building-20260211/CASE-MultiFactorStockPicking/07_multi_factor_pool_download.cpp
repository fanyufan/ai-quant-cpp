// 对应 Python: 多因子选股-下载数据.py
// 实现方式：使用 Tushare stock_basic + fina_indicator 替代 QMT，构建全 A 股财务指标池。
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "csv.hpp"
#include "tushare_client.hpp"

namespace fs = std::filesystem;

static std::optional<double> parse_double(const std::string& s) {
    if (s.empty()) return std::nullopt;
    try {
        size_t pos = 0;
        double v = std::stod(s, &pos);
        if (pos != s.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

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

    std::string output_file = "data/stock_fina_pool_Tushare.csv";
    bool test_mode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--test") {
            test_mode = true;
        }
    }

    fmt::print("多因子选股 - 财务数据下载（Tushare 版）\n");
    fmt::print("输出：{}\n", output_file);
    fmt::print("{:-<60}\n", "");

    try {
        quant::tushare::Client client(token_env);

        fmt::print("获取股票列表...\n");
        auto basic_result = client.stock_basic("", "L", "ts_code,name,industry");
        if (!basic_result.contains("data") || basic_result["data"].empty()) {
            fmt::print("错误：无法获取股票列表\n");
            return 1;
        }

        auto basic_fields = basic_result["data"]["fields"];
        auto basic_items = basic_result["data"]["items"];
        auto get_basic_idx = [&](const std::string& name) -> int {
            for (size_t i = 0; i < basic_fields.size(); ++i) {
                if (basic_fields[i].template get<std::string>() == name) return static_cast<int>(i);
            }
            return -1;
        };
        int ts_idx = get_basic_idx("ts_code");
        int name_idx = get_basic_idx("name");
        int ind_idx = get_basic_idx("industry");

        std::vector<std::tuple<std::string, std::string, std::string>> stocks;
        for (const auto& item : basic_items) {
            std::string code = item[ts_idx].get<std::string>();
            std::string name = name_idx >= 0 ? json_to_string(item[name_idx]) : "";
            std::string ind = ind_idx >= 0 ? json_to_string(item[ind_idx]) : "";
            stocks.emplace_back(code, name, ind);
        }

        if (test_mode && !stocks.empty()) {
            fmt::print("[测试模式] 仅处理前 5 只股票\n");
            stocks.resize(5);
        }

        fmt::print("共 {} 只股票待处理\n", stocks.size());

        const std::string fina_fields =
            "ts_code,end_date,roe,netprofit_yoy,grossprofit_margin,debt_to_assets,ocf_to_revenue";
        std::vector<std::string> out_headers = {
            "stock_code", "stock_name", "industry", "end_date",
            "roe", "netprofit_yoy", "grossprofit_margin",
            "debt_to_assets", "ocf_to_revenue"
        };
        std::vector<std::vector<std::string>> rows;
        rows.reserve(stocks.size());

        size_t success = 0;
        size_t failed = 0;
        for (size_t i = 0; i < stocks.size(); ++i) {
            const auto& [code, name, ind] = stocks[i];
            try {
                auto res = client.fina_indicator(code, "", fina_fields);
                if (!res.contains("data") || res["data"].empty()) {
                    ++failed;
                    continue;
                }
                auto f_fields = res["data"]["fields"];
                auto f_items = res["data"]["items"];
                auto get_fidx = [&](const std::string& n) -> int {
                    for (size_t k = 0; k < f_fields.size(); ++k) {
                        if (f_fields[k].template get<std::string>() == n) return static_cast<int>(k);
                    }
                    return -1;
                };
                int ed_idx = get_fidx("end_date");
                int roe_idx = get_fidx("roe");
                int npy_idx = get_fidx("netprofit_yoy");
                int gm_idx = get_fidx("grossprofit_margin");
                int dta_idx = get_fidx("debt_to_assets");
                int ocf_idx = get_fidx("ocf_to_revenue");

                // 取最新一期
                std::string latest_end;
                size_t latest_row = 0;
                bool found = false;
                for (size_t r = 0; r < f_items.size(); ++r) {
                    std::string ed = json_to_string(f_items[r][ed_idx]);
                    if (ed > latest_end) {
                        latest_end = ed;
                        latest_row = r;
                        found = true;
                    }
                }
                if (!found) {
                    ++failed;
                    continue;
                }

                const auto& item = f_items[latest_row];
                auto get = [&](int idx) -> std::string {
                    if (idx < 0) return "";
                    return json_to_string(item[idx]);
                };

                rows.push_back({
                    code, name, ind, latest_end,
                    get(roe_idx), get(npy_idx), get(gm_idx),
                    get(dta_idx), get(ocf_idx)
                });
                ++success;
            } catch (const std::exception& e) {
                ++failed;
                fmt::print("  {} 处理失败：{}\n", code, e.what());
            }

            if ((i + 1) % 100 == 0 || i + 1 == stocks.size()) {
                fmt::print("\r  进度 {}/{} | 成功 {} | 失败 {}", i + 1, stocks.size(), success, failed);
            }
        }
        fmt::print("\n");

        fs::create_directories(fs::path(output_file).parent_path());
        quant::csv::write_csv(output_file, out_headers, rows);
        fmt::print("\n已保存：{} ({} 只)\n", output_file, rows.size());

        return 0;
    } catch (const std::exception& e) {
        fmt::print("运行错误：{}\n", e.what());
        return 1;
    }
}
