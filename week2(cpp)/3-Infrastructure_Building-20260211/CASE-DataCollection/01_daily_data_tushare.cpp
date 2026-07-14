// 对应 Python: 日线数据-tushare.py
// 实现方式：调用 Tushare daily + adj_factor 接口，本地计算前复权价格。
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "csv.hpp"
#include "date_utils.hpp"
#include "tushare_client.hpp"

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
        fmt::print("步骤1：初始化Tushare Pro...\n");
        quant::tushare::Client client(token_env);
        fmt::print("初始化成功\n");

        fmt::print("\n步骤2：下载日线数据（前复权）...\n");
        auto raw = client.daily(STOCK_CODE, DATA_START, DATA_END);
        auto factors = client.adj_factor(STOCK_CODE, DATA_START, DATA_END);

        if (!raw.contains("data") || raw["data"].empty()) {
            fmt::print("错误：无法获取日线数据，请检查Token权限或股票代码\n");
            return 1;
        }

        auto raw_fields = raw["data"]["fields"];
        auto raw_items = raw["data"]["items"];
        fmt::print("成功获取 {} 条日线原始数据\n", raw_items.size());

        auto get_idx = [](const auto& fields, const std::string& name) -> int {
            for (size_t i = 0; i < fields.size(); ++i) {
                if (fields[i].template get<std::string>() == name) return static_cast<int>(i);
            }
            return -1;
        };

        int date_idx = get_idx(raw_fields, "trade_date");
        int open_idx = get_idx(raw_fields, "open");
        int high_idx = get_idx(raw_fields, "high");
        int low_idx = get_idx(raw_fields, "low");
        int close_idx = get_idx(raw_fields, "close");
        int vol_idx = get_idx(raw_fields, "vol");

        if (date_idx < 0 || close_idx < 0) {
            fmt::print("错误：返回数据缺少必要字段\n");
            return 1;
        }

        // 复权因子
        std::unordered_map<std::string, double> factor_map;
        double latest_factor = 1.0;
        std::string latest_date;
        if (factors.contains("data") && !factors["data"].empty()) {
            auto f_fields = factors["data"]["fields"];
            auto f_items = factors["data"]["items"];
            int f_date_idx = get_idx(f_fields, "trade_date");
            int f_val_idx = get_idx(f_fields, "adj_factor");
            if (f_date_idx >= 0 && f_val_idx >= 0) {
                for (const auto& item : f_items) {
                    std::string d = item[f_date_idx].get<std::string>();
                    double f = item[f_val_idx].get<double>();
                    factor_map[d] = f;
                    if (d > latest_date) {
                        latest_date = d;
                        latest_factor = f;
                    }
                }
            }
        }
        if (latest_date.empty()) {
            fmt::print("警告：未获取到复权因子，将使用未复权价格\n");
            latest_factor = 1.0;
        }

        auto adj_price = [&](double price, const std::string& date_str) -> double {
            auto it = factor_map.find(date_str);
            double f = (it != factor_map.end()) ? it->second : latest_factor;
            return price * f / latest_factor;
        };

        std::vector<std::vector<std::string>> rows;
        for (const auto& item : raw_items) {
            std::string date_str = item[date_idx].get<std::string>();
            quant::date::Date d(date_str);
            if (!d.valid()) continue;

            std::vector<std::string> row;
            row.push_back(d.to_string("-"));
            if (open_idx >= 0) row.push_back(fmt::format("{:.2f}", adj_price(item[open_idx].get<double>(), date_str)));
            if (high_idx >= 0) row.push_back(fmt::format("{:.2f}", adj_price(item[high_idx].get<double>(), date_str)));
            if (low_idx >= 0) row.push_back(fmt::format("{:.2f}", adj_price(item[low_idx].get<double>(), date_str)));
            if (close_idx >= 0) row.push_back(fmt::format("{:.2f}", adj_price(item[close_idx].get<double>(), date_str)));
            if (vol_idx >= 0) row.push_back(fmt::format("{:.0f}", item[vol_idx].get<double>()));
            rows.push_back(row);
        }

        if (rows.empty()) {
            fmt::print("错误：过滤后无有效数据\n");
            return 1;
        }

        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });

        fmt::print("数据日期范围：{} 至 {}\n", rows.front()[0], rows.back()[0]);

        fmt::print("\n步骤3：保存数据到CSV文件...\n");
        fs::create_directories("data");

        std::string code_us = STOCK_CODE;
        std::replace(code_us.begin(), code_us.end(), '.', '_');
        std::string output_file = fmt::format("data/{}_daily_tushare.csv", code_us);

        std::vector<std::string> headers = {"date"};
        if (open_idx >= 0) headers.push_back("open");
        if (high_idx >= 0) headers.push_back("high");
        if (low_idx >= 0) headers.push_back("low");
        if (close_idx >= 0) headers.push_back("close");
        if (vol_idx >= 0) headers.push_back("volume");

        quant::csv::write_csv(output_file, headers, rows);
        fmt::print("数据已保存至：{}\n", output_file);

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

        double min_close = std::numeric_limits<double>::max();
        double max_close = std::numeric_limits<double>::lowest();
        size_t close_col = 0;
        for (size_t j = 0; j < headers.size(); ++j) {
            if (headers[j] == "close") close_col = j;
        }
        bool has_volume = false;
        size_t vol_col = 0;
        for (size_t j = 0; j < headers.size(); ++j) {
            if (headers[j] == "volume") {
                has_volume = true;
                vol_col = j;
            }
        }
        double min_vol = std::numeric_limits<double>::max();
        double max_vol = std::numeric_limits<double>::lowest();
        for (const auto& row : rows) {
            double c = std::stod(row[close_col]);
            min_close = std::min(min_close, c);
            max_close = std::max(max_close, c);
            if (has_volume) {
                double v = std::stod(row[vol_col]);
                min_vol = std::min(min_vol, v);
                max_vol = std::max(max_vol, v);
            }
        }
        fmt::print("\n数据统计信息：\n");
        fmt::print("  总记录数：{}\n", rows.size());
        fmt::print("  收盘价范围：{:.2f} - {:.2f}\n", min_close, max_close);
        if (has_volume) {
            fmt::print("  成交量范围：{:.0f} - {:.0f}\n", min_vol, max_vol);
        }

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
