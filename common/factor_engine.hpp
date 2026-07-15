#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "backtest.hpp"

namespace quant::factor {

struct FactorConfig {
    std::string name;      // factor key
    std::string label;     // Chinese label
    int direction = 1;     // +1 higher better, -1 lower better
    double weight = 0.0;
};

struct ScoredStock {
    std::string code;
    std::map<std::string, double> values;   // raw factor values
    std::map<std::string, double> ranks;    // 0..1 rank pct per factor
    double score = 0.0;
};

// Default 8-factor configuration used by the course.
std::vector<FactorConfig> default_factor_config();

// Compute the default 8 factors for the last bar of a single stock.
// Returns empty map if there are not enough bars.
std::map<std::string, double> calc_all_factors(const std::vector<quant::bt::Bar>& bars);

// Batch compute factors for many stocks. Key: stock code -> factor name -> value.
std::map<std::string, std::map<std::string, double>> batch_calc_factors(
    const std::map<std::string, std::vector<quant::bt::Bar>>& all_data);

// Compute OBV series from bars.
std::vector<double> obv(const std::vector<quant::bt::Bar>& bars);

// Compute cross-sectional rank pct (0..1). NaN for missing/invalid.
std::vector<double> rank_pct(const std::vector<std::optional<double>>& values,
                             bool higher_better = true);

// Score stocks according to the factor configuration.
// Returns vector sorted by score descending.
std::vector<ScoredStock> score_stocks(
    const std::map<std::string, std::map<std::string, double>>& factor_data,
    const std::vector<FactorConfig>& config = default_factor_config());

// Select top-N stock codes. Ties are broken by original order.
std::vector<std::string> select_top_stocks(
    const std::map<std::string, std::map<std::string, double>>& factor_data,
    size_t top_n = 10,
    const std::vector<FactorConfig>& config = default_factor_config());

// Pretty-print a factor report.
void print_factor_report(const std::vector<ScoredStock>& scored,
                         size_t top_n = 10,
                         const std::string& title = "");

} // namespace quant::factor
