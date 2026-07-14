// 对应 Python: 多因子选股-筛选1.py
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "csv.hpp"

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

static std::optional<size_t> col_idx(const quant::csv::CsvData& data, const std::string& name) {
    return data.col_index(name);
}

int main(int argc, char* argv[]) {
    const double ROE_MIN = 15.0;
    const double NETPROFIT_YOY_MIN = 10.0;
    const double GROSSPROFIT_MARGIN_MIN = 30.0;
    const double DEBT_TO_ASSETS_MAX = 60.0;
    const double OCF_TO_REVENUE_MIN = 10.0;

    std::string input_file = "data/stock_fina_pool_QMT.csv";
    std::string output_file = "data/stock_fina_selected_QMT.csv";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--input" || arg == "-i") && i + 1 < argc) {
            input_file = argv[++i];
        } else if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            output_file = argv[++i];
        }
    }

    fmt::print("多因子选股 - 筛选1（通用版）\n");
    fmt::print("ROE >= {}%  |  净利润同比 >= {}%  |  毛利率 >= {}%\n",
               ROE_MIN, NETPROFIT_YOY_MIN, GROSSPROFIT_MARGIN_MIN);
    fmt::print("资产负债率 <= {}%  |  经营现金流/营收 >= {}%\n",
               DEBT_TO_ASSETS_MAX, OCF_TO_REVENUE_MIN);
    fmt::print("{:-<60}\n", "");

    if (!fs::exists(input_file)) {
        fmt::print("错误：未找到输入文件 {}\n", input_file);
        fmt::print("请先运行数据下载脚本或指定 --input 路径\n");
        return 1;
    }

    auto data = quant::csv::read_csv(input_file);
    fmt::print("读取 {} 只股票\n", data.nrow());

    std::vector<char> mask(data.nrow(), 1);

    if (auto idx = col_idx(data, "roe")) {
        size_t remaining = 0;
        for (size_t i = 0; i < data.nrow(); ++i) {
            auto v = parse_double(data.rows[i][*idx]);
            if (!v || *v < ROE_MIN) mask[i] = 0;
            if (mask[i]) ++remaining;
        }
        fmt::print("  第1层 ROE >= {}%: 剩余 {} 只\n", ROE_MIN, remaining);
    }

    if (auto idx = col_idx(data, "netprofit_yoy")) {
        size_t remaining = 0;
        for (size_t i = 0; i < data.nrow(); ++i) {
            if (!mask[i]) continue;
            auto v = parse_double(data.rows[i][*idx]);
            if (!v || *v < NETPROFIT_YOY_MIN) mask[i] = 0;
            if (mask[i]) ++remaining;
        }
        fmt::print("  第2层 净利润同比 >= {}%: 剩余 {} 只\n", NETPROFIT_YOY_MIN, remaining);
    }

    if (auto idx = col_idx(data, "grossprofit_margin")) {
        size_t remaining = 0;
        for (size_t i = 0; i < data.nrow(); ++i) {
            if (!mask[i]) continue;
            auto v = parse_double(data.rows[i][*idx]);
            if (!v || *v < GROSSPROFIT_MARGIN_MIN) mask[i] = 0;
            if (mask[i]) ++remaining;
        }
        fmt::print("  第3层 毛利率 >= {}%: 剩余 {} 只\n", GROSSPROFIT_MARGIN_MIN, remaining);
    }

    if (auto idx = col_idx(data, "debt_to_assets")) {
        size_t remaining = 0;
        for (size_t i = 0; i < data.nrow(); ++i) {
            if (!mask[i]) continue;
            auto v = parse_double(data.rows[i][*idx]);
            if (!v || *v > DEBT_TO_ASSETS_MAX) mask[i] = 0;
            if (mask[i]) ++remaining;
        }
        fmt::print("  第4层 资产负债率 <= {}%: 剩余 {} 只\n", DEBT_TO_ASSETS_MAX, remaining);
    }

    if (auto idx = col_idx(data, "ocf_to_revenue")) {
        size_t remaining = 0;
        for (size_t i = 0; i < data.nrow(); ++i) {
            if (!mask[i]) continue;
            auto v = parse_double(data.rows[i][*idx]);
            if (!v || *v < OCF_TO_REVENUE_MIN) mask[i] = 0;
            if (mask[i]) ++remaining;
        }
        fmt::print("  第5层 经营现金流/营收 >= {}%: 剩余 {} 只\n", OCF_TO_REVENUE_MIN, remaining);
    }

    std::vector<std::vector<std::string>> selected_rows;
    std::vector<double> roe_values;
    std::vector<size_t> selected_indices;
    auto roe_opt = col_idx(data, "roe");
    for (size_t i = 0; i < data.nrow(); ++i) {
        if (!mask[i]) continue;
        selected_rows.push_back(data.rows[i]);
        selected_indices.push_back(i);
        if (roe_opt) {
            auto v = parse_double(data.rows[i][*roe_opt]);
            roe_values.push_back(v.value_or(0.0));
        }
    }

    // 按 ROE 降序
    if (roe_opt && !selected_rows.empty()) {
        std::vector<size_t> order(selected_rows.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return roe_values[a] > roe_values[b];
        });
        std::vector<std::vector<std::string>> sorted;
        sorted.reserve(order.size());
        for (size_t i : order) sorted.push_back(std::move(selected_rows[i]));
        selected_rows = std::move(sorted);
    }

    fs::create_directories(fs::path(output_file).parent_path());
    quant::csv::write_csv(output_file, data.headers, selected_rows);
    fmt::print("\n筛选完成：共 {} 只股票达标\n", selected_rows.size());
    fmt::print("已保存：{}\n", output_file);

    if (!selected_rows.empty()) {
        fmt::print("\n{:=<60}\n", "");
        fmt::print("达标股票（按 ROE 排序）：\n");
        fmt::print("{:=<60}\n", "");
        std::vector<std::string> disp_cols = {
            "stock_code", "stock_name", "end_date", "roe", "netprofit_yoy",
            "grossprofit_margin", "debt_to_assets", "ocf_to_revenue"
        };
        std::vector<size_t> disp_idx;
        for (const auto& c : disp_cols) {
            auto opt = col_idx(data, c);
            if (opt) disp_idx.push_back(*opt);
        }
        for (size_t i = 0; i < std::min<size_t>(20, selected_rows.size()); ++i) {
            for (size_t j = 0; j < disp_idx.size(); ++j) {
                if (j > 0) fmt::print("  ");
                fmt::print("{}", selected_rows[i][disp_idx[j]]);
            }
            fmt::print("\n");
        }
    }

    return 0;
}
