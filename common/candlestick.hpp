#pragma once

#include <string>
#include <vector>
#include <utility>

#include "backtest.hpp"

namespace quant::candle {

// 信号含义：1=看涨，-1=看跌，0=无信号
using Signal = int;

// 单根 K 线形态
Signal doji(const quant::bt::Bar& bar);
Signal hammer(const quant::bt::Bar& bar);
Signal hanging_man(const quant::bt::Bar& bar);
Signal inverted_hammer(const quant::bt::Bar& bar);
Signal shooting_star(const quant::bt::Bar& bar);

// 双 K 线形态
Signal bullish_engulfing(const quant::bt::Bar& prev, const quant::bt::Bar& curr);
Signal bearish_engulfing(const quant::bt::Bar& prev, const quant::bt::Bar& curr);
Signal bullish_harami(const quant::bt::Bar& prev, const quant::bt::Bar& curr);
Signal bearish_harami(const quant::bt::Bar& prev, const quant::bt::Bar& curr);
Signal piercing_pattern(const quant::bt::Bar& prev, const quant::bt::Bar& curr);
Signal dark_cloud_cover(const quant::bt::Bar& prev, const quant::bt::Bar& curr);

// 三 K 线形态
Signal morning_star(const quant::bt::Bar& first, const quant::bt::Bar& second, const quant::bt::Bar& third);
Signal evening_star(const quant::bt::Bar& first, const quant::bt::Bar& second, const quant::bt::Bar& third);

// 汇总扫描：返回 (形态名称, 信号) 列表
std::vector<std::pair<std::string, Signal>> scan(const std::vector<quant::bt::Bar>& bars, size_t i);

// 返回当前 bar 的综合看涨/看跌信号强度（看涨形态数 - 看跌形态数）
int composite_signal(const std::vector<quant::bt::Bar>& bars, size_t i);

} // namespace quant::candle
