// 对应 Python: 财务数据-tushare.py
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "csv.hpp"
#include "tushare_client.hpp"

namespace fs = std::filesystem;

static std::string json_to_string(const nlohmann::json& j) {
    if (j.is_null()) return "";
    if (j.is_string()) return j.get<std::string>();
    return j.dump();
}

int main() {
    const std::string STOCK_CODE = "600519.SH";
    const std::string STOCK_NAME = "贵州茅台";

    const std::string FINA_FIELDS =
        "ts_code,ann_date,end_date,"
        "eps,dt_eps,bps,ocfps,undist_profit_ps,total_revenue_ps,"
        "roe,roe_waa,roe_dt,roa,grossprofit_margin,netprofit_margin,"
        "profit_to_gr,op_of_gr,ebit_of_gr,"
        "debt_to_assets,current_ratio,quick_ratio,cash_ratio,"
        "netprofit_yoy,dt_netprofit_yoy,or_yoy,op_yoy,ocf_yoy,bps_yoy,assets_yoy,eqt_yoy,"
        "assets_turn,inv_turn,ar_turn,ca_turn,fa_turn,invturn_days,arturn_days,"
        "fcff,fcfe,salescash_to_or,ocf_to_or,ocf_to_opincome,"
        "op_income,ebit,ebitda";

    const char* token_env = std::getenv("TUSHARE_TOKEN");
    if (!token_env || std::string(token_env).empty()) {
        fmt::print("错误：未找到环境变量 TUSHARE_TOKEN\n");
        fmt::print("请设置环境变量：set TUSHARE_TOKEN=your_token\n");
        return 1;
    }

    fmt::print("开始下载财务数据\n");
    fmt::print("股票：{}({})\n", STOCK_NAME, STOCK_CODE);
    fmt::print("{:-<60}\n", "");

    try {
        fmt::print("\n步骤1：获取综合财务指标...\n");
        quant::tushare::Client client(token_env);

        size_t field_count = 1;
        for (char c : FINA_FIELDS) if (c == ',') ++field_count;
        fmt::print("  请求字段数：{}\n", field_count);

        auto result = client.fina_indicator(STOCK_CODE, "", FINA_FIELDS);

        if (!result.contains("data") || result["data"].empty()) {
            fmt::print("错误：无法获取财务指标数据，请检查Token权限（需2000积分）\n");
            return 1;
        }

        auto fields = result["data"]["fields"];
        auto items = result["data"]["items"];
        fmt::print("成功获取 {} 期财务数据\n", items.size());

        auto get_idx = [&](const std::string& name) -> int {
            for (size_t i = 0; i < fields.size(); ++i) {
                if (fields[i].get<std::string>() == name) return static_cast<int>(i);
            }
            return -1;
        };

        int end_date_idx = get_idx("end_date");
        if (end_date_idx < 0) {
            fmt::print("错误：返回数据缺少 end_date 字段\n");
            return 1;
        }

        std::vector<std::pair<std::string, std::vector<std::string>>> dated_rows;
        for (const auto& item : items) {
            std::string end_date = json_to_string(item[end_date_idx]);
            std::vector<std::string> row;
            row.reserve(fields.size());
            for (size_t i = 0; i < fields.size(); ++i) {
                row.push_back(json_to_string(item[i]));
            }
            dated_rows.emplace_back(end_date, std::move(row));
        }

        std::sort(dated_rows.begin(), dated_rows.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        fmt::print("报告期范围：{} 至 {}\n", dated_rows.front().first, dated_rows.back().first);

        fmt::print("\n步骤2：保存数据到CSV文件...\n");
        fs::create_directories("data");

        std::string code_us = STOCK_CODE;
        std::replace(code_us.begin(), code_us.end(), '.', '_');
        std::string output_file = fmt::format("data/{}_fina_tushare.csv", code_us);

        std::vector<std::string> headers;
        headers.reserve(fields.size());
        for (const auto& f : fields) {
            headers.push_back(f.get<std::string>());
        }

        std::vector<std::vector<std::string>> rows;
        rows.reserve(dated_rows.size());
        for (auto& p : dated_rows) {
            rows.push_back(std::move(p.second));
        }

        quant::csv::write_csv(output_file, headers, rows);
        fmt::print("数据已保存至：{}\n", output_file);

        // 最近3期转置预览
        fmt::print("\n{:=<60}\n", "");
        fmt::print("数据预览（最近3期）：\n");
        fmt::print("{:=<60}\n", "");
        size_t preview_count = std::min<size_t>(3, rows.size());
        std::vector<size_t> preview_indices;
        for (size_t i = rows.size() - preview_count; i < rows.size(); ++i) {
            preview_indices.push_back(i);
        }
        for (size_t c = 0; c < headers.size(); ++c) {
            fmt::print("{:<20s}", headers[c]);
            for (size_t idx : preview_indices) {
                fmt::print(" {:>16s}", rows[idx][c]);
            }
            fmt::print("\n");
        }

        // 关键指标最新值
        fmt::print("\n{:=<60}\n", "");
        fmt::print("关键指标最新值：\n");
        fmt::print("{:=<60}\n", "");
        const auto& latest = rows.back();
        auto get_val = [&](const std::string& name) -> std::string {
            int idx = get_idx(name);
            if (idx < 0) return "";
            return latest[idx];
        };
        std::vector<std::pair<std::string, std::string>> indicators = {
            {"报告期", get_val("end_date")},
            {"基本每股收益", get_val("eps")},
            {"每股净资产", get_val("bps")},
            {"净资产收益率(%)", get_val("roe")},
            {"总资产报酬率(%)", get_val("roa")},
            {"销售毛利率(%)", get_val("grossprofit_margin")},
            {"销售净利率(%)", get_val("netprofit_margin")},
            {"资产负债率(%)", get_val("debt_to_assets")},
            {"流动比率", get_val("current_ratio")},
            {"净利润同比(%)", get_val("netprofit_yoy")},
            {"营收同比(%)", get_val("or_yoy")},
            {"总资产周转率", get_val("assets_turn")},
            {"存货周转率", get_val("inv_turn")},
            {"每股经营现金流", get_val("ocfps")},
        };
        for (const auto& kv : indicators) {
            fmt::print("  {:<18s}  {}\n", kv.first, kv.second);
        }

        fmt::print("\n数据列（共 {} 个字段）：\n", headers.size());
        for (size_t i = 0; i < headers.size(); ++i) {
            fmt::print("  {:>2d}. {}\n", i + 1, headers[i]);
        }

        fmt::print("\n{:=<60}\n", "");
        fmt::print("财务数据下载完成!\n");
        fmt::print("数据文件：{}\n", output_file);
        fmt::print("{:=<60}\n", "");

        return 0;
    } catch (const std::exception& e) {
        fmt::print("下载数据过程中发生错误：{}\n", e.what());
        return 1;
    }
}
