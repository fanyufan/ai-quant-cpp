// 对应 Python: 2-获取贵州茅台的财务指标.py
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fmt/format.h>
#include "csv.hpp"
#include "date_utils.hpp"
#include "tushare_client.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

int main() {
    const std::string STOCK_NAME = "贵州茅台";
    const std::string STOCK_CODE = "600519.SH";
    const std::string DATA_FILE = "data/600519_SH_daily.csv";

    const char* token_env = std::getenv("TUSHARE_TOKEN");
    if (!token_env || std::string(token_env).empty()) {
        fmt::print("错误：未设置环境变量 TUSHARE_TOKEN\n");
        return 1;
    }

    if (!fs::exists(DATA_FILE)) {
        fmt::print("错误：数据文件不存在 {}\n", DATA_FILE);
        return 1;
    }

    auto csv = quant::csv::read_csv(DATA_FILE);
    if (csv.empty()) {
        fmt::print("错误：无法读取 {}\n", DATA_FILE);
        return 1;
    }

    auto date_col = csv.column("date");
    auto close_col = csv.column_double("close");
    if (date_col.empty() || close_col.empty()) {
        fmt::print("错误：数据缺少 date 或 close 列\n");
        return 1;
    }

    std::string last_date = date_col.back();
    double local_close = close_col.back();
    std::string trade_date = quant::date::format_yyyymmdd(last_date);

    quant::tushare::Client client(token_env);

    json result;
    json row;
    try {
        result = client.daily_basic(trade_date);
        if (!result.contains("data") || result["data"].empty()) {
            throw std::runtime_error("no data");
        }
        // Find the row for our stock
        bool found = false;
        for (const auto& r : result["data"]["items"]) {
            if (r[0].get<std::string>() == STOCK_CODE) {
                row = r;
                found = true;
                break;
            }
        }
        if (!found) throw std::runtime_error("stock not found");
    } catch (const std::exception& e) {
        fmt::print("错误：Tushare 未返回该日基本面数据（daily_basic 需一定积分权限），请检查 TUSHARE_TOKEN 与积分。\n");
        fmt::print("详细错误：{}\n", e.what());
        return 1;
    }

    // Parse fields: ts_code, trade_date, close, pe, pb, total_share, total_mv, float_share, circ_mv
    auto fields = result.value("data", json{}).value("fields", json::array());
    auto get_idx = [&](const std::string& name) -> int {
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].get<std::string>() == name) return static_cast<int>(i);
        }
        return -1;
    };

    double price = local_close;
    int close_idx = get_idx("close");
    if (close_idx >= 0 && !row[close_idx].is_null()) {
        price = row[close_idx].get<double>();
    }

    std::string trade_date_str = trade_date;
    int td_idx = get_idx("trade_date");
    if (td_idx >= 0 && !row[td_idx].is_null()) {
        trade_date_str = row[td_idx].get<std::string>();
    }
    std::string date_display = fmt::format("{}-{}-{}", trade_date_str.substr(0, 4),
                                           trade_date_str.substr(4, 2), trade_date_str.substr(6, 2));

    auto get_double = [&](const std::string& name) -> std::optional<double> {
        int idx = get_idx(name);
        if (idx < 0 || row[idx].is_null()) return std::nullopt;
        return row[idx].get<double>();
    };

    auto total_share_wan = get_double("total_share");
    auto total_mv_wan = get_double("total_mv");
    auto pe = get_double("pe");
    auto pb = get_double("pb");

    std::optional<double> total_shares;
    std::optional<double> total_mv_yi;
    std::optional<double> market_cap;

    if (total_share_wan && *total_share_wan > 0) {
        total_shares = *total_share_wan * 10000.0;
    }
    if (total_mv_wan && *total_mv_wan > 0) {
        total_mv_yi = *total_mv_wan / 10000.0;
    }
    if (total_shares) {
        market_cap = price * *total_shares;
    } else if (total_mv_wan) {
        market_cap = *total_mv_wan * 10000.0;
    }

    std::optional<double> eps, bps;
    if (pe && *pe > 0) eps = price / *pe;
    if (pb && *pb > 0) bps = price / *pb;

    fmt::print("数据来源：Tushare daily_basic（真实数据）\n");
    fmt::print("基准日期：{}\n", date_display);
    fmt::print("收盘价：{:.2f} 元\n", price);
    if (total_shares) {
        fmt::print("总股本：{:.2f} 亿股\n", *total_shares / 1e8);
    }
    if (total_mv_yi) {
        fmt::print("总市值（Tushare）：{:.2f} 亿元\n", *total_mv_yi);
    }
    if (eps) {
        fmt::print("每股收益(EPS)：{:.2f} 元（由 股价/PE 反推）\n", *eps);
    }
    if (bps) {
        fmt::print("每股净资产(BPS)：{:.2f} 元（由 股价/PB 反推）\n", *bps);
    }
    fmt::print("{:-<60}\n", "");
    if (market_cap && total_shares) {
        fmt::print("市值 = 股价 * 总股本 = {:.2f} * {:.2f}亿 = {:.2f} 亿元\n",
                   price, *total_shares / 1e8, *market_cap / 1e8);
    } else if (total_mv_yi) {
        fmt::print("总市值：{:.2f} 亿元（Tushare）\n", *total_mv_yi);
    }
    if (pe) {
        fmt::print("PE(市盈率) = {:.2f} （约{:.0f}年回本）\n", *pe, *pe);
    }
    if (pb) {
        fmt::print("PB(市净率) = {:.2f}\n", *pb);
    }
    fmt::print("{:-<60}\n", "");
    fmt::print("说明：PE 越低越便宜，但成长股可容忍高 PE；PB<1 为破净，常见于银行、钢铁。\n");

    return 0;
}
