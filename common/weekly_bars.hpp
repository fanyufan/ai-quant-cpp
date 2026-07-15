#pragma once

#include <vector>
#include "backtest.hpp"

namespace quant::bt {

// Return the Monday date (yyyy-mm-dd) of the calendar week containing date.
std::string week_start(const std::string& date);

// Resample daily bars into weekly bars (Monday-based calendar week).
// The returned bar's date is the last trading day in that week.
std::vector<Bar> resample_to_weekly(const std::vector<Bar>& daily);

} // namespace quant::bt
