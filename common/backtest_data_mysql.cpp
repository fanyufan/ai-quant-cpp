#include "backtest_data_mysql.hpp"

#include <map>
#include <sstream>

#include <mysql.h>

namespace quant::bt::data {

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

} // namespace quant::bt::data
