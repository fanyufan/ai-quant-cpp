#include "grid_engine.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace quant::grid {

GridEngine::GridEngine(double upper, double lower, int num_grids, double total_capital) {
    init(upper, lower, num_grids, total_capital);
}

void GridEngine::init(double upper, double lower, int num_grids, double total_capital) {
    upper_ = upper;
    lower_ = lower;
    num_grids_ = num_grids;
    grid_size_ = (upper - lower) / static_cast<double>(num_grids);
    capital_per_grid_ = total_capital / static_cast<double>(num_grids);
    position_at_.assign(num_grids, 0);
    levels_.resize(num_grids + 1);
    for (int i = 0; i <= num_grids; ++i) {
        levels_[i] = lower + grid_size_ * static_cast<double>(i);
    }
    prev_cell_.reset();
    buy_count_ = 0;
    sell_count_ = 0;
    total_profit_ = 0.0;
    max_layers_ = 0;
}

int GridEngine::get_cell(double price) const {
    if (price < lower_) return -1;
    if (price >= upper_) return num_grids_;
    return static_cast<int>((price - lower_) / grid_size_);
}

int GridEngine::calc_shares(double price) const {
    if (price <= 0.0) return 0;
    int shares = static_cast<int>(capital_per_grid_ / price);
    shares = (shares / 100) * 100;
    if (shares < 100) shares = 100;
    return shares;
}

int GridEngine::current_layers() const {
    int layers = 0;
    for (int p : position_at_) if (p > 0) ++layers;
    return layers;
}

bool GridEngine::is_out_of_range(double price) const {
    return price < lower_ || price > upper_;
}

std::vector<GridSignal> GridEngine::update(double price) {
    std::vector<GridSignal> signals;
    int curr = get_cell(price);
    if (!prev_cell_.has_value()) {
        prev_cell_ = curr;
        return signals;
    }
    int prev = prev_cell_.value();
    if (curr < prev) {
        for (int cell = prev - 1; cell >= curr; --cell) {
            if (cell >= 0 && cell < num_grids_ && position_at_[cell] == 0) {
                GridSignal sig;
                sig.action = GridAction::Buy;
                sig.level = cell;
                sig.price = levels_[cell];
                sig.shares = calc_shares(levels_[cell]);
                position_at_[cell] = sig.shares;
                ++buy_count_;
                signals.push_back(sig);
            }
        }
    } else if (curr > prev) {
        for (int cell = prev; cell < curr; ++cell) {
            if (cell >= 0 && cell < num_grids_ && position_at_[cell] > 0) {
                GridSignal sig;
                sig.action = GridAction::Sell;
                sig.level = cell;
                sig.price = levels_[cell + 1];
                sig.shares = position_at_[cell];
                sig.profit = (sig.price - levels_[cell]) * static_cast<double>(sig.shares);
                total_profit_ += sig.profit;
                position_at_[cell] = 0;
                ++sell_count_;
                signals.push_back(sig);
            }
        }
    }
    prev_cell_ = curr;
    int layers = current_layers();
    if (layers > max_layers_) max_layers_ = layers;
    return signals;
}

GridEngine::Stats GridEngine::get_stats() const {
    Stats s;
    s.buy_count = buy_count_;
    s.sell_count = sell_count_;
    s.total_profit = total_profit_;
    s.max_layers = max_layers_;
    s.current_layers = current_layers();
    if (buy_count_ > 0) {
        s.grid_utilization = static_cast<double>(sell_count_) / static_cast<double>(buy_count_);
    }
    return s;
}

void GridEngine::summary(const std::string& title) const {
    auto s = get_stats();
    fmt::print("\n{}\n", title);
    fmt::print("  价格区间: {:.3f} ~ {:.3f} | 网格数: {}\n", lower_, upper_, num_grids_);
    fmt::print("  买入次数: {} | 卖出次数: {} | 最大层数: {}\n",
               s.buy_count, s.sell_count, s.max_layers);
    fmt::print("  网格利用率: {:.1f}% | 总利润: {:.2f}\n",
               s.grid_utilization * 100.0, s.total_profit);
}

ChanGridEngine::ChanGridEngine(double zg, double zd, int num_grids, double total_capital) {
    init(zg, zd, num_grids, total_capital);
}

void ChanGridEngine::switch_zhongshu(double zg, double zd) {
    // Reset positions when switching to a new中枢.
    init(zg, zd, num_grids_, capital_per_grid_ * num_grids_);
    active_ = true;
}

bool ChanGridEngine::is_in_zhongshu(double price) const {
    return price >= lower_ && price <= upper_;
}

bool ChanGridEngine::is_breakout_up(double price) const {
    return price > upper_;
}

bool ChanGridEngine::is_breakdown(double price) const {
    return price < lower_;
}

std::vector<GridSignal> ChanGridEngine::update(double price) {
    if (!active_) return {};
    if (is_breakout_up(price) || is_breakdown(price)) {
        active_ = false;
        return {};
    }
    return GridEngine::update(price);
}

} // namespace quant::grid
