#pragma once

#include "env.hpp"

namespace quant::bt {

struct BacktestParams {
    double initial_cash = 1'000'000.0;
    double commission = 0.0002;
    double position_pct = 95.0; // simple sizer percentage
};

BacktestParams load_backtest_params(const quant::env::EnvMap& env);

} // namespace quant::bt
