#pragma once

#include <map>
#include <string>
#include <vector>

#include "backtest.hpp"
#include "mysql_client.hpp"

namespace quant::bt::data {

// 从 MySQL trade_stock_daily 加载指定股票的日线
std::vector<Bar> load_from_mysql(const quant::mysql::Config& cfg,
                                 const std::string& stock_code,
                                 const std::string& start_date = "",
                                 const std::string& end_date = "");

// 加载全表数据并按 stock_code 分组（用于选股雷达等全市场扫描）
std::map<std::string, std::vector<Bar>> load_all_from_mysql(const quant::mysql::Config& cfg);

} // namespace quant::bt::data
