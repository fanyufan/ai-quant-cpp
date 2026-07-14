#pragma once

#include <string>
#include <vector>

#include "backtest.hpp"

namespace quant::bt::data {

// 从 CSV 加载日线，列名需包含 date/close/open/high/low/volume
// start_date/end_date 为空字符串表示不限制，格式 YYYY-MM-DD
std::vector<Bar> load_from_csv(const std::string& path,
                               const std::string& start_date = "",
                               const std::string& end_date = "");

// 按日期范围过滤已排序的 Bar 序列
std::vector<Bar> filter_by_date(const std::vector<Bar>& bars,
                                const std::string& start_date,
                                const std::string& end_date);

} // namespace quant::bt::data
