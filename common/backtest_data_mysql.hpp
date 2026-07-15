#pragma once

#include <map>
#include <string>
#include <vector>

#include "backtest.hpp"
#include "env.hpp"
#include "mysql_client.hpp"

namespace quant::bt::data {

// 从 .env 风格配置构造 MySQL 配置
quant::mysql::Config load_mysql_config(const quant::env::EnvMap& env);

// 从 MySQL trade_stock_daily 加载指定股票的日线
std::vector<Bar> load_from_mysql(const quant::mysql::Config& cfg,
                                 const std::string& stock_code,
                                 const std::string& start_date = "",
                                 const std::string& end_date = "");

// 加载全表数据并按 stock_code 分组（用于选股雷达等全市场扫描）
std::map<std::string, std::vector<Bar>> load_all_from_mysql(const quant::mysql::Config& cfg);

// 查询有数据的股票/ETF代码列表（可选日期范围）
std::vector<std::string> list_available_symbols(const quant::mysql::Config& cfg,
                                                const std::string& start_date = "",
                                                const std::string& end_date = "");

// 从 trade_stock_status 查询标的名称 {code: name}
std::map<std::string, std::string> get_symbol_names(const quant::mysql::Config& cfg,
                                                    const std::vector<std::string>& codes);

// 查询指定代码在各年份的数据天数 {code: {year: count}}
std::map<std::string, std::map<int, size_t>> get_symbol_data_summary(
    const quant::mysql::Config& cfg,
    const std::vector<std::string>& codes,
    const std::string& start_date = "",
    const std::string& end_date = "");

} // namespace quant::bt::data
