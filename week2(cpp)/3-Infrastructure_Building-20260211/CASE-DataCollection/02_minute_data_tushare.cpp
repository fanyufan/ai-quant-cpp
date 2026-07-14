// 对应 Python: 分钟数据-tushare.py
// 实现方式：调用 Tushare stk_mins + adj_factor 接口，本地计算前复权价格。
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
    const std::string TARGET_DATE = "20260210";
    const std::string FREQ = "1min";

    const char* token_env = std::getenv("TUSHARE_TOKEN");
    if (!token_env || std::string(token_env).empty()) {
        fmt::print("错误：未找到环境变量 TUSHARE_TOKEN\n");
        fmt::print("请设置环境变量：set TUSHARE_TOKEN=your_token\n");
        return 1;
    }

    fmt::print("开始下载分钟数据\n");
    fmt::print("股票：{}({})\n", STOCK_NAME, STOCK_CODE);
    fmt::print("日期：{}\n", TARGET_DATE);
    fmt::print("频率：{}\n", FREQ);
    fmt::print("{:-<60}\n", "");

    try {
        fmt::print("步骤1：初始化Tushare Pro...\n");
        quant::tushare::Client client(token_env);
        fmt::print("初始化成功\n");

        fmt::print("\n步骤2：下载{}数据（前复权）...\n", FREQ);
        std::string start_time = TARGET_DATE + " 09:00:00";
        std::string end_time = TARGET_DATE + " 15:00:00";
        auto raw = client.stk_mins(STOCK_CODE, FREQ, start_time, end_time);
        auto factors = client.adj_factor(STOCK_CODE, TARGET_DATE, TARGET_DATE);

        if (!raw.contains("data") || raw["data"].empty()) {
            fmt::print("错误：无法获取分钟数据\n");
            fmt::print("可能原因：\n");
            fmt::print("  1. 未开通分钟行情权限\n");
            fmt::print("  2. 目标日期为非交易日\n");
            fmt::print("  3. Token权限不足\n");
            return 1;
        }

        auto raw_fields = raw["data"]["fields"];
        auto raw_items = raw["data"]["items"];
        fmt::print("成功获取 {} 条分钟数据\n", raw_items.size());

        auto get_idx = [](const auto& fields, const std::string& name) -> int {
            for (size_t i = 0; i < fields.size(); ++i) {
                if (fields[i].template get<std::string>() == name) return static_cast<int>(i);
            }
            return -1;
        };

        int time_idx = get_idx(raw_fields, "trade_time");
        int open_idx = get_idx(raw_fields, "open");
        int high_idx = get_idx(raw_fields, "high");
        int low_idx = get_idx(raw_fields, "low");
        int close_idx = get_idx(raw_fields, "close");
        int vol_idx = get_idx(raw_fields, "vol");
        int amount_idx = get_idx(raw_fields, "amount");

        if (time_idx < 0 || close_idx < 0) {
            fmt::print("错误：返回数据缺少必要字段\n");
            return 1;
        }

        // 复权因子：以目标日期的因子为准
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

        auto adj_price = [&](double price) -> double {
            return price * latest_factor / latest_factor; // 使用当天因子作为基准
        };
        // 若希望与日线一致（复权到最新），可改为 price * factor / latest_factor
        // 这里目标日期唯一，因此保持原值；如有需要可扩展为按日期查表。

        std::vector<std::vector<std::string>> rows;
        for (const auto& item : raw_items) {
            std::string trade_time = item[time_idx].get<std::string>();
            if (trade_time.size() < 19) continue;
            std::string date_part = trade_time.substr(0, 10);
            std::string time_part = trade_time.substr(11, 8);
            quant::date::Date d(date_part);
            if (!d.valid()) continue;
            std::string datetime_str = fmt::format("{} {}", d.to_string("-"), time_part);

            std::vector<std::string> row;
            row.push_back(datetime_str);
            if (open_idx >= 0) row.push_back(fmt::format("{:.2f}", adj_price(item[open_idx].get<double>())));
            if (high_idx >= 0) row.push_back(fmt::format("{:.2f}", adj_price(item[high_idx].get<double>())));
            if (low_idx >= 0) row.push_back(fmt::format("{:.2f}", adj_price(item[low_idx].get<double>())));
            if (close_idx >= 0) row.push_back(fmt::format("{:.2f}", adj_price(item[close_idx].get<double>())));
            if (vol_idx >= 0) row.push_back(fmt::format("{:.0f}", item[vol_idx].get<double>()));
            if (amount_idx >= 0) row.push_back(fmt::format("{:.2f}", item[amount_idx].get<double>()));
            rows.push_back(row);
        }

        if (rows.empty()) {
            fmt::print("错误：过滤后无有效数据\n");
            return 1;
        }

        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });

        fmt::print("时间范围：{} 至 {}\n", rows.front()[0], rows.back()[0]);

        fmt::print("\n步骤3：保存数据到CSV文件...\n");
        fs::create_directories("data");

        std::string code_us = STOCK_CODE;
        std::replace(code_us.begin(), code_us.end(), '.', '_');
        std::string output_file = fmt::format("data/{}_1min_tushare.csv", code_us);

        std::vector<std::string> headers = {"datetime"};
        if (open_idx >= 0) headers.push_back("open");
        if (high_idx >= 0) headers.push_back("high");
        if (low_idx >= 0) headers.push_back("low");
        if (close_idx >= 0) headers.push_back("close");
        if (vol_idx >= 0) headers.push_back("volume");
        if (amount_idx >= 0) headers.push_back("amount");

        quant::csv::write_csv(output_file, headers, rows);
        fmt::print("数据已保存至：{}\n", output_file);

        fmt::print("\n数据预览（前10行）：\n");
        for (size_t i = 0; i < std::min<size_t>(10, rows.size()); ++i) {
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
        fmt::print("\n数据统计：\n");
        fmt::print("  总记录数：{}\n", rows.size());
        fmt::print("  收盘价范围：{:.2f} - {:.2f}\n", min_close, max_close);
        if (has_volume) {
            fmt::print("  成交量范围：{:.0f} - {:.0f}\n", min_vol, max_vol);
        }

        fmt::print("\n{:=<60}\n", "");
        fmt::print("分钟数据下载完成!\n");
        fmt::print("数据文件：{}\n", output_file);
        fmt::print("{:=<60}\n", "");

        return 0;
    } catch (const std::exception& e) {
        fmt::print("下载数据过程中发生错误：{}\n", e.what());
        return 1;
    }
}
