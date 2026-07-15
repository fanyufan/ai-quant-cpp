#include "backtest_data_mysql.hpp"

#include <map>
#include <sstream>

#include <mysql.h>

namespace quant::bt::data {

quant::mysql::Config load_mysql_config(const quant::env::EnvMap& env) {
    quant::mysql::Config cfg;
    cfg.host = quant::env::get_string(env, "WUCAI_SQL_HOST",
        quant::env::get_string(env, "MYSQL_HOST", "localhost"));
    cfg.port = quant::env::get_int(env, "WUCAI_SQL_PORT",
        quant::env::get_int(env, "MYSQL_PORT", 3306));
    cfg.user = quant::env::get_string(env, "WUCAI_SQL_USERNAME",
        quant::env::get_string(env, "MYSQL_USER", "root"));
    cfg.password = quant::env::get_string(env, "WUCAI_SQL_PASSWORD",
        quant::env::get_string(env, "MYSQL_PASSWORD", ""));
    cfg.database = quant::env::get_string(env, "WUCAI_SQL_DB",
        quant::env::get_string(env, "MYSQL_DB", "wucai_trade"));
    return cfg;
}

std::vector<Bar> load_from_mysql(const quant::mysql::Config& cfg,
                                 const std::string& stock_code,
                                 const std::string& start_date,
                                 const std::string& end_date) {
    quant::mysql::Client client(cfg);
    if (!client.connect()) {
        return {};
    }

    std::ostringstream sql;
    sql << "SELECT trade_date, open_price, high_price, low_price, close_price, volume "
        << "FROM trade_stock_daily WHERE stock_code = '" << stock_code << "'";
    if (!start_date.empty()) {
        sql << " AND trade_date >= '" << start_date << "'";
    }
    if (!end_date.empty()) {
        sql << " AND trade_date <= '" << end_date << "'";
    }
    sql << " ORDER BY trade_date";

    if (mysql_query(client.raw(), sql.str().c_str()) != 0) {
        return {};
    }

    MYSQL_RES* res = mysql_store_result(client.raw());
    if (!res) return {};

    std::vector<Bar> bars;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        Bar bar;
        bar.date = row[0] ? row[0] : "";
        bar.open = row[1] ? std::stod(row[1]) : 0.0;
        bar.high = row[2] ? std::stod(row[2]) : 0.0;
        bar.low = row[3] ? std::stod(row[3]) : 0.0;
        bar.close = row[4] ? std::stod(row[4]) : 0.0;
        bar.volume = row[5] ? std::stod(row[5]) : 0.0;
        bars.push_back(bar);
    }
    mysql_free_result(res);
    return bars;
}

std::map<std::string, std::vector<Bar>> load_all_from_mysql(const quant::mysql::Config& cfg) {
    std::map<std::string, std::vector<Bar>> result;
    quant::mysql::Client client(cfg);
    if (!client.connect()) {
        return result;
    }

    std::string sql =
        "SELECT stock_code, trade_date, open_price, high_price, low_price, close_price, volume "
        "FROM trade_stock_daily ORDER BY stock_code, trade_date";

    if (mysql_query(client.raw(), sql.c_str()) != 0) {
        return result;
    }

    MYSQL_RES* res = mysql_store_result(client.raw());
    if (!res) return result;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        std::string code = row[0] ? row[0] : "";
        Bar bar;
        bar.date = row[1] ? row[1] : "";
        bar.open = row[2] ? std::stod(row[2]) : 0.0;
        bar.high = row[3] ? std::stod(row[3]) : 0.0;
        bar.low = row[4] ? std::stod(row[4]) : 0.0;
        bar.close = row[5] ? std::stod(row[5]) : 0.0;
        bar.volume = row[6] ? std::stod(row[6]) : 0.0;
        result[code].push_back(bar);
    }
    mysql_free_result(res);
    return result;
}



std::vector<std::string> list_available_symbols(const quant::mysql::Config& cfg,
                                                const std::string& start_date,
                                                const std::string& end_date) {
    std::vector<std::string> symbols;
    quant::mysql::Client client(cfg);
    if (!client.connect()) return symbols;

    std::ostringstream sql;
    sql << "SELECT DISTINCT stock_code FROM trade_stock_daily";
    bool has_where = false;
    if (!start_date.empty()) {
        sql << " WHERE trade_date >= '" << start_date << "'";
        has_where = true;
    }
    if (!end_date.empty()) {
        sql << (has_where ? " AND" : " WHERE") << " trade_date <= '" << end_date << "'";
    }
    sql << " ORDER BY stock_code";

    if (mysql_query(client.raw(), sql.str().c_str()) != 0) return symbols;
    MYSQL_RES* res = mysql_store_result(client.raw());
    if (!res) return symbols;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        if (row[0]) symbols.push_back(row[0]);
    }
    mysql_free_result(res);
    return symbols;
}

std::map<std::string, std::string> get_symbol_names(const quant::mysql::Config& cfg,
                                                    const std::vector<std::string>& codes) {
    std::map<std::string, std::string> names;
    if (codes.empty()) return names;

    quant::mysql::Client client(cfg);
    if (!client.connect()) return names;

    std::ostringstream sql;
    sql << "SELECT stock_code, stock_name FROM trade_stock_status WHERE stock_code IN (";
    for (size_t i = 0; i < codes.size(); ++i) {
        if (i > 0) sql << ",";
        sql << "'" << codes[i] << "'";
    }
    sql << ")";

    if (mysql_query(client.raw(), sql.str().c_str()) != 0) return names;
    MYSQL_RES* res = mysql_store_result(client.raw());
    if (!res) return names;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        std::string code = row[0] ? row[0] : "";
        std::string name = row[1] && row[1][0] ? row[1] : code;
        names[code] = name;
    }
    mysql_free_result(res);
    return names;
}

std::map<std::string, std::map<int, size_t>> get_symbol_data_summary(
    const quant::mysql::Config& cfg,
    const std::vector<std::string>& codes,
    const std::string& start_date,
    const std::string& end_date) {
    std::map<std::string, std::map<int, size_t>> summary;
    if (codes.empty()) return summary;

    quant::mysql::Client client(cfg);
    if (!client.connect()) return summary;

    std::ostringstream sql;
    sql << "SELECT stock_code, YEAR(trade_date) AS yr, COUNT(*) AS cnt FROM trade_stock_daily "
        << "WHERE stock_code IN (";
    for (size_t i = 0; i < codes.size(); ++i) {
        if (i > 0) sql << ",";
        sql << "'" << codes[i] << "'";
    }
    sql << ")";
    if (!start_date.empty()) sql << " AND trade_date >= '" << start_date << "'";
    if (!end_date.empty()) sql << " AND trade_date <= '" << end_date << "'";
    sql << " GROUP BY stock_code, YEAR(trade_date)";

    if (mysql_query(client.raw(), sql.str().c_str()) != 0) return summary;
    MYSQL_RES* res = mysql_store_result(client.raw());
    if (!res) return summary;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        std::string code = row[0] ? row[0] : "";
        int year = row[1] ? std::stoi(row[1]) : 0;
        size_t cnt = row[2] ? std::stoull(row[2]) : 0;
        summary[code][year] = cnt;
    }
    mysql_free_result(res);
    return summary;
}

std::map<std::string, std::vector<Bar>> batch_load_daily(
    const quant::mysql::Config& cfg,
    const std::string& start_date,
    const std::string& end_date,
    size_t min_bars) {
    std::map<std::string, std::vector<Bar>> result;
    quant::mysql::Client client(cfg);
    if (!client.connect()) return result;

    std::ostringstream sql;
    sql << "SELECT stock_code, trade_date, open_price, high_price, low_price, "
        << "close_price, volume FROM trade_stock_daily "
        << "WHERE trade_date >= '" << start_date << "' AND trade_date <= '" << end_date << "' "
        << "ORDER BY stock_code, trade_date ASC";

    if (mysql_query(client.raw(), sql.str().c_str()) != 0) return result;
    MYSQL_RES* res = mysql_store_result(client.raw());
    if (!res) return result;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        std::string code = row[0] ? row[0] : "";
        Bar bar;
        bar.date = row[1] ? row[1] : "";
        bar.open = row[2] ? std::stod(row[2]) : 0.0;
        bar.high = row[3] ? std::stod(row[3]) : 0.0;
        bar.low = row[4] ? std::stod(row[4]) : 0.0;
        bar.close = row[5] ? std::stod(row[5]) : 0.0;
        bar.volume = row[6] ? std::stod(row[6]) : 0.0;
        result[code].push_back(bar);
    }
    mysql_free_result(res);

    for (auto it = result.begin(); it != result.end();) {
        if (it->second.size() < min_bars) {
            it = result.erase(it);
        } else {
            ++it;
        }
    }
    return result;
}

std::map<std::string, std::map<std::string, std::vector<FinancialRecord>>> load_financial_data(
    const quant::mysql::Config& cfg,
    const std::vector<std::string>& fields,
    const std::string& report_date_min) {
    std::map<std::string, std::map<std::string, std::vector<FinancialRecord>>> result;
    if (fields.empty()) return result;

    quant::mysql::Client client(cfg);
    if (!client.connect()) return result;

    std::ostringstream sql;
    sql << "SELECT stock_code, report_date";
    for (const auto& f : fields) sql << ", " << f;
    sql << " FROM trade_stock_financial WHERE 1=1";
    if (!report_date_min.empty()) {
        sql << " AND report_date >= '" << report_date_min << "'";
    }
    sql << " ORDER BY stock_code, report_date ASC";

    if (mysql_query(client.raw(), sql.str().c_str()) != 0) return result;
    MYSQL_RES* res = mysql_store_result(client.raw());
    if (!res) return result;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        std::string code = row[0] ? row[0] : "";
        std::string date = row[1] ? row[1] : "";
        for (size_t i = 0; i < fields.size(); ++i) {
            if (row[i + 2] && row[i + 2][0]) {
                FinancialRecord rec;
                rec.date = date;
                rec.value = std::stod(row[i + 2]);
                result[code][fields[i]].push_back(rec);
            }
        }
    }
    mysql_free_result(res);
    return result;
}

std::map<std::string, double> load_latest_total_assets(
    const quant::mysql::Config& cfg,
    const std::string& report_date) {
    std::map<std::string, double> out;
    quant::mysql::Client client(cfg);
    if (!client.connect()) return out;

    std::ostringstream sql;
    sql << "SELECT f.stock_code, f.total_assets FROM trade_stock_financial f "
        << "INNER JOIN (SELECT stock_code, MAX(report_date) AS max_date FROM trade_stock_financial";
    if (!report_date.empty()) {
        sql << " WHERE report_date <= '" << report_date << "'";
    }
    sql << " GROUP BY stock_code) m ON f.stock_code=m.stock_code AND f.report_date=m.max_date";

    if (mysql_query(client.raw(), sql.str().c_str()) != 0) return out;
    MYSQL_RES* res = mysql_store_result(client.raw());
    if (!res) return out;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        std::string code = row[0] ? row[0] : "";
        if (row[1] && row[1][0]) {
            out[code] = std::stod(row[1]);
        }
    }
    mysql_free_result(res);
    return out;
}

} // namespace quant::bt::data
