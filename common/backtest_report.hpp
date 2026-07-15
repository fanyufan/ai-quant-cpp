#pragma once

#include <string>
#include "backtest.hpp"

namespace quant::bt {

struct Metrics {
    double final_value = 0.0;
    double total_return = 0.0;       // fraction, e.g. 0.12
    double annual_return = 0.0;
    double max_drawdown = 0.0;       // positive fraction, e.g. 0.10
    size_t max_dd_len = 0;
    double sharpe_ratio = 0.0;
    double calmar_ratio = 0.0;
    size_t total_trades = 0;
    size_t won_trades = 0;
    size_t lost_trades = 0;
    double win_rate = 0.0;
    double avg_win = 0.0;
    double avg_loss = 0.0;
    double profit_loss_ratio = 0.0;
    double profit_factor = 0.0;
    size_t max_consecutive_losses = 0;
    double expected_value = 0.0;
    double years = 0.0;
    size_t trading_days = 0;
};

// Compute full performance metrics from a backtest Result.
Metrics compute_metrics(const Result& result, double risk_free_rate_annual = 0.02);

// Print a single-line summary (similar to data_loader run_and_report).
void print_metrics_line(const Metrics& m);

// Print a compact table comparing buy&hold vs strategies.
void print_three_strategy_table(double bh_return,
                                 const Metrics& simple,
                                 const Metrics& full);

} // namespace quant::bt
