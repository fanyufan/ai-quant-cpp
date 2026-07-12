// 对应 Python: 1-tushare_download_data.py
#include <iostream>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>
#include <limits>
#include <filesystem>
#include <fmt/format.h>
#include "tushare_client.hpp"
#include "csv.hpp"
#include "date_utils.hpp"

namespace fs = std::filesystem;

int main() {
    const std::string STOCK_CODE = "600519.SH";
    const std::string STOCK_NAME = "贵州茅台";
    const std::string DATA_START = "20240101";
    const std::string DATA_END = "20251231";

    const char* token_env = std::getenv("TUSHARE_TOKEN");
    if (!token_env || std::string(token_env).empty()) {
        fmt::print("错误：未找到环境变量 TUSHARE_TOKEN\n");
        fmt::print("请设置环境变量：set TUSHARE_TOKEN=your_token\n");
        return 1;
    }

    fmt::print("开始下载股票数据\n");
    fmt::print("股票：{}({})\n", STOCK_NAME, STOCK_CODE);
    fmt::print("日期范围：{} 至 {}\n", DATA_START, DATA_END);
    fmt::print("{:-<60}\n", "");

    try {
        fmt::print("步骤1：初始化tushare...\n");
        quant::tushare::Client client(token_env);
        fmt::print("tushare初始化成功\n");

        fmt::print("\n步骤2：下载历史数据...\n");
        auto result = client.daily(STOCK_CODE, DATA_START, DATA_END);

        if (!result.contains("data") || result["data"].empty()) {
            fmt::print("错误：无法获取历史数据\n");
            return 1;
        }

        auto fields = result["data"]["fields"];
        auto items = result["data"]["items"];
        fmt::print("成功获取 {} 条历史数据\n", items.size());

        auto get_idx = [&](const std::string& name) -> int {
            for (size_t i = 0; i < fields.size(); ++i) {
                if (fields[i].get<std::string>() == name) return static_cast<int>(i);
            }
            return -1;
        };

        int date_idx = get_idx("trade_date");
        int open_idx = get_idx("open");
        int high_idx = get_idx("high");
        int low_idx = get_idx("low");
        int close_idx = get_idx("close");
        int vol_idx = get_idx("vol");

        std::vector<std::vector<std::string>> rows;
        for (const auto& item : items) {
            std::string date_str = item[date_idx].get<std::string>();
            quant::date::Date d(date_str);
            if (!d.valid()) continue;

            std::vector<std::string> row;
            row.push_back(d.to_string("-"));
            if (close_idx >= 0) row.push_back(fmt::format("{:.2f}", item[close_idx].get<double>()));
            if (open_idx >= 0) row.push_back(fmt::format("{:.2f}", item[open_idx].get<double>()));
            if (high_idx >= 0) row.push_back(fmt::format("{:.2f}", item[high_idx].get<double>()));
            if (low_idx >= 0) row.push_back(fmt::format("{:.2f}", item[low_idx].get<double>()));
            if (vol_idx >= 0) row.push_back(fmt::format("{:.0f}", item[vol_idx].get<double>()));
            rows.push_back(row);
        }

        if (rows.empty()) {
            fmt::print("错误：过滤后无有效数据\n");
            return 1;
        }

        // Sort by date ascending
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            return a[0] < b[0];
        });

        fmt::print("数据日期范围：{} 至 {}\n", rows.front()[0], rows.back()[0]);

        fmt::print("\n步骤3：保存数据到CSV文件...\n");
        fs::create_directories("data");
        std::string output_file = fmt::format("data/{}_daily.csv",
                                              std::string(STOCK_CODE).replace(STOCK_CODE.find("."), 1, "_"));

        std::vector<std::string> headers = {"date", "close"};
        if (open_idx >= 0) headers.push_back("open");
        if (high_idx >= 0) headers.push_back("high");
        if (low_idx >= 0) headers.push_back("low");
        if (vol_idx >= 0) headers.push_back("volume");

        quant::csv::write_csv(output_file, headers, rows);
        fmt::print("数据已保存至：{}\n", output_file);

        // Preview
        fmt::print("\n数据预览（前5行）：\n");
        for (size_t i = 0; i < std::min<size_t>(5, rows.size()); ++i) {
            for (size_t j = 0; j < headers.size(); ++j) {
                if (j > 0) fmt::print(" ");
                fmt::print("{}", rows[i][j]);
            }
            fmt::print("\n");
        }
        fmt::print("\n数据预览（后5行）：\n");
        for (size_t i = rows.size() - std::min<size_t>(5, rows.size()); i < rows.size(); ++i) {
            for (size_t j = 0; j < headers.size(); ++j) {
                if (j > 0) fmt::print(" ");
                fmt::print("{}", rows[i][j]);
            }
            fmt::print("\n");
        }

        // Stats
        double min_close = std::numeric_limits<double>::max();
        double max_close = std::numeric_limits<double>::lowest();
        for (const auto& row : rows) {
            double c = std::stod(row[1]);
            min_close = std::min(min_close, c);
            max_close = std::max(max_close, c);
        }
        fmt::print("\n数据统计信息：\n");
        fmt::print("  总记录数：{}\n", rows.size());
        fmt::print("  收盘价范围：{:.2f} - {:.2f}\n", min_close, max_close);

        fmt::print("\n{:=<60}\n", "");
        fmt::print("数据下载完成!\n");
        fmt::print("数据文件：{}\n", output_file);
        fmt::print("{:=<60}\n", "");

        return 0;
    } catch (const std::exception& e) {
        fmt::print("下载数据过程中发生错误：{}\n", e.what());
        return 1;
    }
}
