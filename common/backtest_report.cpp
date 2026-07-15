#include "backtest_report.hpp"

#include <cmath>
#include <fmt/format.h>
#include <numeric>

namespace quant::bt {

namespace {

struct TradePnl {
    double pnl = 0.0;
    bool win = false;
};

std::vector<TradePnl> match_trades(const std::vector<Trade>& trades) {
    std::vector<TradePnl> closed;
    struct Lot { double price; int shares; };
    std::vector<Lot> lots;

    for (const auto& t : trades) {
        if (t.action == "BUY") {
            lots.push_back({t.price, t.shares});
        } else if (t.action == "SELL") {
            int remaining = t.shares;
            double total_pnl = 0.0;
            while (remaining > 0 && !lots.empty()) {
                int use = std::min(remaining, lots.front().shares);
                total_pnl += (t.price - lots.front().price) * use;
                lots.front().shares -= use;
                remaining -= use;
                if (lots.front().shares == 0) lots.erase(lots.begin());
            }
            closed.push_back({total_pnl, total_pnl > 0.0});
        }
    }
    return closed;
}

} // namespace

Metrics compute_metrics(const Result& result, double risk_free_rate_annual) {
    Metrics m;
    m.final_value = result.final_nav;
    m.trading_days = result.records.size();
    m.years = m.trading_days / 252.0;
    m.total_return = result.total_return;

    if (m.years > 0.0 && m.total_return > -1.0) {
        m.annual_return = std::pow(1.0 + m.total_return, 1.0 / m.years) - 1.0;
    } else {
        m.annual_return = m.total_return;
    }

    // Drawdown and daily returns
    double peak = result.init_cash;
    size_t dd_len = 0;
    size_t max_dd_len = 0;
    std::vector<double> daily_returns;
    daily_returns.reserve(m.trading_days);
    for (size_t i = 0; i < result.records.size(); ++i) {
        double nav = result.records[i].nav;
        if (i > 0) {
            double prev_nav = result.records[i - 1].nav;
            if (prev_nav > 0.0) daily_returns.push_back(nav / prev_nav - 1.0);
        }
        if (nav > peak) {
            peak = nav;
            dd_len = 0;
        } else {
            ++dd_len;
            if (dd_len > max_dd_len) max_dd_len = dd_len;
        }
        double dd = (peak - nav) / peak;
        if (dd > m.max_drawdown) m.max_drawdown = dd;
    }
    m.max_dd_len = max_dd_len;

    m.calmar_ratio = m.max_drawdown > 0.0 ? m.annual_return / m.max_drawdown : 0.0;

    // Sharpe
    if (!daily_returns.empty()) {
        double mean = std::accumulate(daily_returns.begin(), daily_returns.end(), 0.0) / daily_returns.size();
        double sq_sum = 0.0;
        for (double r : daily_returns) sq_sum += (r - mean) * (r - mean);
        double stddev = std::sqrt(sq_sum / daily_returns.size());
        double daily_rf = risk_free_rate_annual / 252.0;
        if (stddev > 0.0) {
            m.sharpe_ratio = (mean - daily_rf) / stddev * std::sqrt(252.0);
        }
    }

    // Trade stats
    auto pnls = match_trades(result.trades);
    m.total_trades = pnls.size();
    double gross_profit = 0.0;
    double gross_loss = 0.0;
    double win_sum = 0.0;
    double loss_sum = 0.0;
    size_t max_lose_streak = 0;
    size_t current_lose_streak = 0;
    for (const auto& p : pnls) {
        if (p.win) {
            ++m.won_trades;
            win_sum += p.pnl;
            gross_profit += p.pnl;
            current_lose_streak = 0;
        } else {
            ++m.lost_trades;
            loss_sum += p.pnl;
            gross_loss += p.pnl;
            ++current_lose_streak;
            if (current_lose_streak > max_lose_streak) max_lose_streak = current_lose_streak;
        }
    }
    m.max_consecutive_losses = max_lose_streak;
    m.win_rate = m.total_trades > 0 ? static_cast<double>(m.won_trades) / m.total_trades : 0.0;
    m.avg_win = m.won_trades > 0 ? win_sum / m.won_trades : 0.0;
    m.avg_loss = m.lost_trades > 0 ? loss_sum / m.lost_trades : 0.0;
    m.profit_loss_ratio = m.avg_loss != 0.0 ? std::abs(m.avg_win / m.avg_loss) : 0.0;
    m.profit_factor = gross_loss != 0.0 ? std::abs(gross_profit / gross_loss) : 0.0;
    if (m.total_trades > 0) {
        m.expected_value = m.win_rate * m.avg_win + (1.0 - m.win_rate) * m.avg_loss;
    }

    return m;
}

void print_metrics_line(const Metrics& m) {
    fmt::print("  总收益: {:+.2f}% | 年化: {:+.2f}% | 最大回撤: {:.2f}% | 夏普: {:.2f} | 卡玛: {:.2f}\n",
               m.total_return * 100.0, m.annual_return * 100.0, m.max_drawdown * 100.0,
               m.sharpe_ratio, m.calmar_ratio);
    fmt::print("  交易: {}次 | 胜率: {:.1f}% | 盈亏比: {:.2f} | 利润因子: {:.2f} | 最大连亏: {}次\n",
               m.total_trades, m.win_rate * 100.0, m.profit_loss_ratio, m.profit_factor,
               m.max_consecutive_losses);
}

void print_three_strategy_table(double bh_return,
                                 const Metrics& simple,
                                 const Metrics& full) {
    double bh_pct = (bh_return * 100.0);
    fmt::print("  {:<12} {:>12} {:>12} {:>12}\n", "指标", "买入持有", "简单海龟", "完整海龟");
    fmt::print("  {}\n", std::string(52, '-'));
    fmt::print("  {:<12} {:>+11.2f}% {:>+11.2f}% {:>+11.2f}%\n",
               "总收益", bh_pct, simple.total_return * 100.0, full.total_return * 100.0);
    fmt::print("  {:<12} {:>12} {:>11.2f}% {:>11.2f}%\n",
               "最大回撤", "--", simple.max_drawdown * 100.0, full.max_drawdown * 100.0);
    fmt::print("  {:<12} {:>12} {:>12.2f} {:>12.2f}\n",
               "夏普比率", "--", simple.sharpe_ratio, full.sharpe_ratio);
    fmt::print("  {:<12} {:>12} {:>12d} {:>12d}\n",
               "交易次数", "--", simple.total_trades, full.total_trades);
    fmt::print("  {:<12} {:>12} {:>11.1f}% {:>11.1f}%\n",
               "胜率", "--", simple.win_rate * 100.0, full.win_rate * 100.0);
    fmt::print("  {:<12} {:>12} {:>12.2f} {:>12.2f}\n",
               "盈亏比", "--", simple.profit_loss_ratio, full.profit_loss_ratio);
}

} // namespace quant::bt
