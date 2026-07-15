// 1-经典网格策略
// 对应 week5/10-网格与多因子-20260318/CASE-网格与多因子/1-经典网格策略.py

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <iostream>
#include <vector>

#include "backtest.hpp"
#include "backtest_config.hpp"
#include "backtest_data_mysql.hpp"
#include "backtest_plot.hpp"
#include "backtest_report.hpp"
#include "env.hpp"
#include "grid_engine.hpp"

using namespace quant;

class GridStrategy : public bt::Strategy {
public:
    GridStrategy(double grid_capital_ratio, int lookback, int num_grids, double margin_pct)
        : grid_capital_ratio_(grid_capital_ratio), lookback_(lookback),
          num_grids_(num_grids), margin_pct_(margin_pct) {}

    void init(const std::vector<bt::Bar>& bars) override {
        bars_ = &bars;
        double hist_high = 0.0, hist_low = std::numeric_limits<double>::max();
        for (int i = 0; i < lookback_ && static_cast<size_t>(i) < bars.size(); ++i) {
            hist_high = std::max(hist_high, bars[i].high);
            hist_low = std::min(hist_low, bars[i].low);
        }
        double upper = hist_high * (1.0 + margin_pct_);
        double lower = hist_low * (1.0 - margin_pct_);
        double grid_capital = initial_cash_ * grid_capital_ratio_;
        grid_.init(upper, lower, num_grids_, grid_capital);
    }

    void next(size_t idx, const std::vector<bt::Bar>& bars, bt::Broker& broker) override {
        if (idx < static_cast<size_t>(lookback_)) return;
        double close = bars[idx].close;
        auto signals = grid_.update(close);
        for (const auto& sig : signals) {
            if (sig.action == grid::GridAction::Buy) {
                double target_cash = sig.price * sig.shares;
                if (target_cash > broker.cash()) target_cash = broker.cash();
                broker.buy(idx, bars, target_cash);
            } else {
                int shares = std::min(sig.shares, broker.shares());
                if (shares > 0) broker.sell(idx, bars, shares);
            }
        }
    }

    void set_initial_cash(double cash) { initial_cash_ = cash; }
    const grid::GridEngine& grid() const { return grid_; }

private:
    double grid_capital_ratio_;
    int lookback_;
    int num_grids_;
    double margin_pct_;
    double initial_cash_ = 0.0;
    const std::vector<bt::Bar>* bars_ = nullptr;
    grid::GridEngine grid_;
};

struct ResultEntry {
    std::string code;
    std::string name;
    double total_return = 0.0;
    double max_drawdown = 0.0;
    double sharpe = 0.0;
    size_t trades = 0;
    double win_rate = 0.0;
    double pl_ratio = 0.0;
    double bh_return = 0.0;
};

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);
    auto params = bt::load_backtest_params(env);

    const std::string start_date = "2024-01-01";
    const std::string end_date = "2025-12-31";

    std::vector<std::pair<std::string, std::string>> targets = {
        {"510300.SH", "沪深300ETF"},
        {"600519.SH", "贵州茅台"},
        {"000001.SZ", "平安银行"},
        {"159941.SZ", "纳指ETF"},
    };

    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("经典网格策略回测\n");
    fmt::print("{0}\n", std::string(70, '='));

    std::vector<ResultEntry> rows;
    for (const auto& [code, name] : targets) {
        auto bars = bt::data::load_from_mysql(cfg, code, start_date, end_date);
        if (bars.empty()) {
            fmt::print("  {}({}): 无数据\n", name, code);
            continue;
        }
        if (bars.size() < 60) {
            fmt::print("  {}({}): 数据不足\n", name, code);
            continue;
        }

        GridStrategy strategy(0.90, 60, 8, 0.02);
        strategy.set_initial_cash(params.initial_cash);
        bt::Backtest bt(params.initial_cash, params.commission);
        auto result = bt.run(bars, strategy);
        auto metrics = bt::compute_metrics(result);

        ResultEntry entry;
        entry.code = code;
        entry.name = name;
        entry.total_return = metrics.total_return;
        entry.max_drawdown = metrics.max_drawdown;
        entry.sharpe = metrics.sharpe_ratio;
        entry.trades = metrics.total_trades;
        entry.win_rate = metrics.win_rate;
        entry.pl_ratio = metrics.profit_loss_ratio;
        entry.bh_return = bars.back().close / bars.front().close - 1.0;
        rows.push_back(entry);

        fmt::print("\n[{}] {}\n", name, code);
        bt::print_metrics_line(metrics);
        fmt::print("  买入持有: {:+.2f}% | 网格总利润: {:.2f}\n",
                   entry.bh_return * 100.0, strategy.grid().get_stats().total_profit);

        std::string safe_name = name;
        for (auto& c : safe_name) if (c == ' ' || c == '/') c = '_';
        bt::plot_backtest(result, bars, code, name, "outputs/" + safe_name + "-网格.png");
    }

    fmt::print("\n多标的汇总对比:\n");
    fmt::print("  {:<12} {:>10} {:>10} {:>10} {:>8} {:>10} {:>10}\n",
               "标的", "网格收益", "买入持有", "最大回撤", "夏普", "胜率", "交易数");
    for (const auto& r : rows) {
        fmt::print("  {:<12} {:>+9.2f}% {:>+9.2f}% {:>9.2f}% {:>8.2f} {:>9.1f}% {:>10d}\n",
                   r.name, r.total_return * 100.0, r.bh_return * 100.0,
                   r.max_drawdown * 100.0, r.sharpe, r.win_rate * 100.0, r.trades);
    }

    return 0;
}
