// 对应 Python: 9-形态选股雷达.py
// 实现方式：从 MySQL trade_stock_daily 加载全量日线，扫描 MACD 底背离 + 看涨 K 线形态。
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <mysql.h>

#include "backtest.hpp"
#include "backtest_data_mysql.hpp"
#include "candlestick.hpp"
#include "csv.hpp"
#include "env.hpp"
#include "indicators.hpp"

namespace fs = std::filesystem;

static bool bullish_macd_divergence(const std::vector<quant::bt::Bar>& bars, size_t lookback = 20) {
    if (bars.size() < lookback + 5) return false;
    size_t n = bars.size();
    std::vector<double> closes;
    closes.reserve(n);
    for (const auto& b : bars) closes.push_back(b.close);
    auto macd = quant::ind::macd(closes, 12, 26, 9);

    size_t start = n - lookback;
    // 找最近 lookback 内的最低价及对应 MACD dif
    auto price_min_it = std::min_element(bars.begin() + start, bars.end(),
                                         [](const auto& a, const auto& b) { return a.low < b.low; });
    size_t price_min_idx = static_cast<size_t>(price_min_it - bars.begin());

    // 找 price_min 之前的近期低点（简化：lookback 起点）
    size_t prev_idx = start;
    if (bars[price_min_idx].low >= bars[prev_idx].low) return false;
    if (std::isnan(macd.dif[price_min_idx]) || std::isnan(macd.dif[prev_idx])) return false;

    // 价格创新低，MACD dif 未创新低（底背离）
    return macd.dif[price_min_idx] > macd.dif[prev_idx];
}

int main(int argc, char* argv[]) {
    std::string env_file = ".env";
    std::string output_dir = "outputs";
    int limit = 0; // 0 表示不限制扫描股票数
    int lookback = 20;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--env-file" && i + 1 < argc) env_file = argv[++i];
        else if (arg == "--output-dir" && i + 1 < argc) output_dir = argv[++i];
        else if (arg == "--limit" && i + 1 < argc) limit = std::stoi(argv[++i]);
        else if (arg == "--lookback" && i + 1 < argc) lookback = std::stoi(argv[++i]);
    }

    auto env_map = quant::env::load(env_file);
    quant::mysql::Config mysql_cfg;
    mysql_cfg.host = quant::env::get(env_map, "MYSQL_HOST");
    if (mysql_cfg.host.empty()) mysql_cfg.host = "localhost";
    {
        std::string port_str = quant::env::get(env_map, "MYSQL_PORT");
        mysql_cfg.port = port_str.empty() ? 3306 : std::stoi(port_str);
    }
    mysql_cfg.user = quant::env::get(env_map, "MYSQL_USER");
    mysql_cfg.password = quant::env::get(env_map, "MYSQL_PASSWORD");
    mysql_cfg.database = quant::env::get(env_map, "MYSQL_DB");
    if (mysql_cfg.database.empty()) mysql_cfg.database = "quant";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--mysql-host") && i + 1 < argc) mysql_cfg.host = argv[++i];
        else if ((arg == "--mysql-port") && i + 1 < argc) mysql_cfg.port = std::stoi(argv[++i]);
        else if ((arg == "--mysql-user") && i + 1 < argc) mysql_cfg.user = argv[++i];
        else if ((arg == "--mysql-password") && i + 1 < argc) mysql_cfg.password = argv[++i];
        else if ((arg == "--mysql-db") && i + 1 < argc) mysql_cfg.database = argv[++i];
    }

    fmt::print("形态选股雷达 (MySQL -> CSV)\n");
    fmt::print("[MySQL] {}@{}:{}/{}\n", mysql_cfg.user, mysql_cfg.host, mysql_cfg.port, mysql_cfg.database);
    fmt::print("{:-<60}\n", "");

    quant::mysql::Client client(mysql_cfg);
    if (!client.connect()) {
        fmt::print("错误：无法连接 MySQL：{}\n", client.last_error());
        return 1;
    }

    std::string sql =
        "SELECT stock_code, trade_date, open_price, high_price, low_price, close_price, volume "
        "FROM trade_stock_daily ORDER BY stock_code, trade_date";

    auto stock_bars = quant::bt::data::load_all_from_mysql(mysql_cfg);
    fmt::print("共加载 {} 只股票\n", stock_bars.size());

    std::vector<std::vector<std::string>> candidates;
    std::vector<std::string> headers = {
        "stock_code", "latest_date", "close_price", "pattern_score", "macd_divergence", "data_source"
    };

    int processed = 0;
    for (const auto& [code, bars] : stock_bars) {
        if (limit > 0 && processed >= limit) break;
        ++processed;

        if (bars.size() < static_cast<size_t>(lookback + 26)) continue;

        int pattern_score = quant::candle::composite_signal(bars, bars.size() - 1);
        bool divergence = bullish_macd_divergence(bars, static_cast<size_t>(lookback));

        if (pattern_score > 0 || divergence) {
            const auto& last = bars.back();
            candidates.push_back({
                code,
                last.date,
                fmt::format("{:.2f}", last.close),
                fmt::format("{}", pattern_score),
                divergence ? "1" : "0",
                "mysql"
            });
        }

        if (processed % 100 == 0) {
            fmt::print("\r  已处理 {}/{} | 候选 {} 只", processed, stock_bars.size(), candidates.size());
        }
    }
    fmt::print("\r  已处理 {}/{} | 候选 {} 只\n", processed, stock_bars.size(), candidates.size());

    fs::create_directories(output_dir);
    std::string csv_path = fmt::format("{}/pattern_scanner.csv", output_dir);
    quant::csv::write_csv(csv_path, headers, candidates);
    fmt::print("候选结果已保存：{} ({} 条)\n", csv_path, candidates.size());

    return 0;
}
