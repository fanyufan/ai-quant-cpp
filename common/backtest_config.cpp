#include "backtest_config.hpp"

namespace quant::bt {

BacktestParams load_backtest_params(const quant::env::EnvMap& env) {
    BacktestParams p;
    p.initial_cash = quant::env::get_double(env, "BACKTEST_INITIAL_CASH", 1'000'000.0);
    p.commission = quant::env::get_double(env, "BACKTEST_COMMISSION", 0.0002);
    p.position_pct = quant::env::get_double(env, "BACKTEST_POSITION_PCT", 95.0);
    return p;
}

} // namespace quant::bt
