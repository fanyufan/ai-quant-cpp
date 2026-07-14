// 对应 Python: 1-行情数据采集.py
// 实现方式：使用 Tushare daily 接口替代 MiniQMT，输出 CSV + SQL 文件，可选直接写入 MySQL。
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "csv.hpp"
#include "date_utils.hpp"
#include "env.hpp"
#include "mysql_client.hpp"
#include "tushare_client.hpp"

namespace fs = std::filesystem;

static std::string today_yyyymmdd() {
    std::time_t now = std::time(nullptr);
    std::tm* lt = std::localtime(&now);
    return fmt::format("{:04d}{:02d}{:02d}", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
}

static std::string json_to_string(const nlohmann::json& j) {
    if (j.is_null()) return "";
    if (j.is_string()) return j.get<std::string>();
    return j.dump();
}

static std::string build_insert_query(const std::vector<std::vector<std::string>>& rows) {
    if (rows.empty()) return "";
    std::string sql =
        "INSERT INTO trade_stock_daily "
        "(stock_code, trade_date, open_price, high_price, low_price, close_price, volume, amount, turnover_rate) "
        "VALUES ";
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        if (i) sql += ", ";
        sql += "('" + row[0] + "', '" + row[1] + "', "
            + (row[2].empty() ? "NULL" : row[2]) + ", "
            + (row[3].empty() ? "NULL" : row[3]) + ", "
            + (row[4].empty() ? "NULL" : row[4]) + ", "
            + (row[5].empty() ? "NULL" : row[5]) + ", "
            + (row[6].empty() ? "NULL" : row[6]) + ", "
            + (row[7].empty() ? "NULL" : row[7]) + ", "
            + (row[8].empty() ? "NULL" : row[8]) + ")";
    }
    sql +=
        " ON DUPLICATE KEY UPDATE "
        "open_price=VALUES(open_price), high_price=VALUES(high_price), "
        "low_price=VALUES(low_price), close_price=VALUES(close_price), "
        "volume=VALUES(volume), amount=VALUES(amount), turnover_rate=VALUES(turnover_rate)";
    return sql;
}

static bool create_trade_stock_daily_table(quant::mysql::Client& db) {
    const char* sql = R"(
CREATE TABLE IF NOT EXISTS trade_stock_daily (
    stock_code VARCHAR(16) NOT NULL,
    trade_date DATE NOT NULL,
    open_price DECIMAL(19, 4),
    high_price DECIMAL(19, 4),
    low_price DECIMAL(19, 4),
    close_price DECIMAL(19, 4),
    volume BIGINT,
    amount DECIMAL(19, 4),
    turnover_rate DECIMAL(10, 4),
    PRIMARY KEY (stock_code, trade_date),
    INDEX idx_trade_date (trade_date)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
)";
    return db.execute(sql);
}

int main(int argc, char* argv[]) {
    const char* token_env = std::getenv("TUSHARE_TOKEN");
    if (!token_env || std::string(token_env).empty()) {
        fmt::print("错误：未找到环境变量 TUSHARE_TOKEN\n");
        return 1;
    }

    bool test_mode = true;
    std::string test_stock = "600519.SH";
    std::string data_start = "20230101";
    std::string output_dir = "data";
    std::string env_file = ".env";

    // 第一遍：先找 --env-file
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--env-file" && i + 1 < argc) {
            env_file = argv[++i];
        }
    }

    // 从 .env 加载配置（找不到文件则返回空 map）
    auto env_map = quant::env::load(env_file);

    // MySQL 配置优先从 .env 读取
    bool write_mysql = false;
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

    // 第二遍：命令行参数覆盖 .env
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--full") {
            test_mode = false;
        } else if ((arg == "--test-stock") && i + 1 < argc) {
            test_stock = argv[++i];
        } else if ((arg == "--start") && i + 1 < argc) {
            data_start = argv[++i];
        } else if ((arg == "--output-dir") && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (arg == "--env-file") {
            if (i + 1 < argc) ++i; // 已处理
        } else if (arg == "--write-mysql") {
            write_mysql = true;
        } else if ((arg == "--mysql-host") && i + 1 < argc) {
            mysql_cfg.host = argv[++i];
        } else if ((arg == "--mysql-port") && i + 1 < argc) {
            mysql_cfg.port = std::stoi(argv[++i]);
        } else if ((arg == "--mysql-user") && i + 1 < argc) {
            mysql_cfg.user = argv[++i];
        } else if ((arg == "--mysql-password") && i + 1 < argc) {
            mysql_cfg.password = argv[++i];
        } else if ((arg == "--mysql-db") && i + 1 < argc) {
            mysql_cfg.database = argv[++i];
        }
    }

    std::string csv_path = fmt::format("{}/trade_stock_daily.csv", output_dir);
    std::string sql_path = fmt::format("{}/trade_stock_daily.sql", output_dir);
    std::string end_date = today_yyyymmdd();

    fmt::print("行情数据采集 (Tushare -> CSV/SQL");
    if (write_mysql) {
        fmt::print("/MySQL");
    }
    fmt::print(")\n");
    if (test_mode) {
        fmt::print("[测试模式] 只采集 {}\n", test_stock);
    } else {
        fmt::print("[全量模式] 采集沪深A股全量日线\n");
    }
    if (write_mysql) {
        fmt::print("[MySQL] {}@{}:{}/{}\n", mysql_cfg.user, mysql_cfg.host, mysql_cfg.port, mysql_cfg.database);
    }
    fmt::print("{:-<60}\n", "");

    try {
        quant::tushare::Client client(token_env);

        // 初始化 MySQL 连接（如果需要）
        std::unique_ptr<quant::mysql::Client> db;
        if (write_mysql) {
            db = std::make_unique<quant::mysql::Client>(mysql_cfg);
            if (!db->connect()) {
                fmt::print("错误：无法连接 MySQL：{}\n", db->last_error());
                return 1;
            }
            if (!create_trade_stock_daily_table(*db)) {
                fmt::print("错误：无法创建表：{}\n", db->last_error());
                return 1;
            }
            fmt::print("MySQL 连接成功，表已就绪\n");
        }

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

        // 读取已有 CSV，构建每只股票最新日期
        std::unordered_map<std::string, std::string> existing_latest;
        std::vector<std::vector<std::string>> all_rows;
        std::vector<std::string> headers = {
            "stock_code", "trade_date", "open_price", "high_price", "low_price",
            "close_price", "volume", "amount", "turnover_rate"
        };
        if (fs::exists(csv_path)) {
            auto old = quant::csv::read_csv(csv_path);
            auto code_idx_opt = old.col_index("stock_code");
            auto date_idx_opt = old.col_index("trade_date");
            if (code_idx_opt && date_idx_opt) {
                size_t ci = *code_idx_opt;
                size_t di = *date_idx_opt;
                for (const auto& row : old.rows) {
                    all_rows.push_back(row);
                    const std::string& code = row[ci];
                    quant::date::Date d(row[di]);
                    if (d.valid()) {
                        std::string ymd = d.to_yyyymmdd();
                        auto it = existing_latest.find(code);
                        if (it == existing_latest.end() || ymd > it->second) {
                            existing_latest[code] = ymd;
                        }
                    }
                }
            }
        }

        size_t total = codes.size();
        size_t success = 0;
        size_t skipped = 0;
        size_t new_rows = 0;
        size_t mysql_rows = 0;

        for (size_t i = 0; i < codes.size(); ++i) {
            const std::string& code = codes[i];
            std::string start = data_start;
            auto it = existing_latest.find(code);
            if (it != existing_latest.end()) {
                if (it->second >= end_date) {
                    ++skipped;
                    continue;
                }
                start = it->second;
            }

            std::vector<std::vector<std::string>> stock_rows;
            try {
                auto res = client.daily(code, start, end_date);
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
                int td_idx = get_idx("trade_date");
                int o_idx = get_idx("open");
                int h_idx = get_idx("high");
                int l_idx = get_idx("low");
                int c_idx = get_idx("close");
                int v_idx = get_idx("vol");
                int a_idx = get_idx("amount");
                if (td_idx < 0) continue;

                for (const auto& item : items) {
                    std::string td = json_to_string(item[td_idx]);
                    quant::date::Date d(td);
                    if (!d.valid()) continue;
                    // 跳过已存在日期（如果按原 start 拉取会包含 start 当天）
                    if (it != existing_latest.end() && td <= it->second) continue;

                    std::vector<std::string> row = {
                        code,
                        d.to_string("-"),
                        o_idx >= 0 ? fmt::format("{:.2f}", item[o_idx].get<double>()) : "",
                        h_idx >= 0 ? fmt::format("{:.2f}", item[h_idx].get<double>()) : "",
                        l_idx >= 0 ? fmt::format("{:.2f}", item[l_idx].get<double>()) : "",
                        c_idx >= 0 ? fmt::format("{:.2f}", item[c_idx].get<double>()) : "",
                        v_idx >= 0 ? fmt::format("{:.0f}", item[v_idx].get<double>()) : "",
                        a_idx >= 0 ? fmt::format("{:.2f}", item[a_idx].get<double>()) : "",
                        "" // turnover_rate
                    };
                    all_rows.push_back(row);
                    stock_rows.push_back(row);
                    ++new_rows;
                }
                ++success;

                // 写入 MySQL（分批）
                if (write_mysql && !stock_rows.empty()) {
                    constexpr size_t batch_size = 500;
                    for (size_t b = 0; b < stock_rows.size(); b += batch_size) {
                        auto end = std::min(b + batch_size, stock_rows.size());
                        std::vector<std::vector<std::string>> batch(stock_rows.begin() + b, stock_rows.begin() + end);
                        std::string sql = build_insert_query(batch);
                        if (!db->execute(sql)) {
                            fmt::print("\n错误：写入 MySQL 失败：{}\n", db->last_error());
                            break;
                        }
                        mysql_rows += batch.size();
                    }
                }
            } catch (const std::exception& e) {
                fmt::print("  {} 失败：{}\n", code, e.what());
            }

            if ((i + 1) % 10 == 0 || i + 1 == total) {
                fmt::print("\r  进度 {}/{} | 成功 {} | 跳过 {} | 新增 {} 条",
                           i + 1, total, success, skipped, new_rows);
                if (write_mysql) {
                    fmt::print(" | MySQL 写入 {} 条", mysql_rows);
                }
            }
        }
        fmt::print("\n");

        fs::create_directories(output_dir);
        quant::csv::write_csv(csv_path, headers, all_rows);
        fmt::print("CSV 已保存：{} ({} 条)\n", csv_path, all_rows.size());

        // 生成 SQL
        std::ofstream sql_ofs(sql_path, std::ios::binary);
        sql_ofs.write("\xEF\xBB\xBF", 3);
        sql_ofs << "-- trade_stock_daily insert statements (generated by 08_market_data_collection.cpp)\n";
        for (const auto& row : all_rows) {
            sql_ofs << "INSERT INTO trade_stock_daily "
                       "(stock_code, trade_date, open_price, high_price, low_price, close_price, volume, amount, turnover_rate) "
                       "VALUES ('" << row[0] << "', '" << row[1] << "', "
                    << (row[2].empty() ? "NULL" : row[2]) << ", "
                    << (row[3].empty() ? "NULL" : row[3]) << ", "
                    << (row[4].empty() ? "NULL" : row[4]) << ", "
                    << (row[5].empty() ? "NULL" : row[5]) << ", "
                    << (row[6].empty() ? "NULL" : row[6]) << ", "
                    << (row[7].empty() ? "NULL" : row[7]) << ", "
                    << (row[8].empty() ? "NULL" : row[8]) << ") "
                       "ON DUPLICATE KEY UPDATE "
                       "open_price=VALUES(open_price), high_price=VALUES(high_price), "
                       "low_price=VALUES(low_price), close_price=VALUES(close_price), "
                       "volume=VALUES(volume), amount=VALUES(amount), turnover_rate=VALUES(turnover_rate);\n";
        }
        fmt::print("SQL 已保存：{}\n", sql_path);

        if (write_mysql) {
            fmt::print("MySQL 共写入 {} 条\n", mysql_rows);
            db->close();
        }

        return 0;
    } catch (const std::exception& e) {
        fmt::print("运行错误：{}\n", e.what());
        return 1;
    }
}
