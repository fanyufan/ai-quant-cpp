#pragma once

#include <optional>
#include <string>
#include <vector>

namespace quant::grid {

enum class GridAction { Buy, Sell };

struct GridSignal {
    GridAction action = GridAction::Buy;
    int level = 0;
    int shares = 0;
    double price = 0.0;
    double profit = 0.0;  // only meaningful for Sell
};

class GridEngine {
public:
    GridEngine() = default;
    GridEngine(double upper, double lower, int num_grids, double total_capital);

    void init(double upper, double lower, int num_grids, double total_capital);

    int get_cell(double price) const;
    int calc_shares(double price) const;
    int current_layers() const;
    bool is_out_of_range(double price) const;

    std::vector<GridSignal> update(double price);

    struct Stats {
        int buy_count = 0;
        int sell_count = 0;
        double total_profit = 0.0;
        int max_layers = 0;
        int current_layers = 0;
        double grid_utilization = 0.0;  // sell_count / buy_count
    };
    Stats get_stats() const;
    void summary(const std::string& title = "Grid Summary") const;

protected:
    double upper_ = 0.0;
    double lower_ = 0.0;
    int num_grids_ = 0;
    double grid_size_ = 0.0;
    double capital_per_grid_ = 0.0;
    std::vector<int> position_at_;
    std::vector<double> levels_;
    std::optional<int> prev_cell_;
    int buy_count_ = 0;
    int sell_count_ = 0;
    double total_profit_ = 0.0;
    int max_layers_ = 0;
};

class ChanGridEngine : public GridEngine {
public:
    ChanGridEngine() = default;
    ChanGridEngine(double zg, double zd, int num_grids, double total_capital);

    void switch_zhongshu(double zg, double zd);
    void deactivate() { active_ = false; }
    bool active() const { return active_; }
    bool is_in_zhongshu(double price) const;
    bool is_breakout_up(double price) const;
    bool is_breakdown(double price) const;

    std::vector<GridSignal> update(double price);

private:
    bool active_ = true;
};

} // namespace quant::grid
