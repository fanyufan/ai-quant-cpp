#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "backtest.hpp"

namespace quant::ml {

// Factor taxonomy from feature_engine.py (6 categories, ~50 features).
// Returns category key -> ordered feature names.
std::map<std::string, std::vector<std::string>> feature_taxonomy();

// Returns all feature names in taxonomy order.
std::vector<std::string> all_feature_names();

// Compute ~50 technical features from a single stock's OHLCV bars.
// Returns map feature_name -> vector (same length as bars). Invalid values are NaN.
std::map<std::string, std::vector<double>> calc_features(
    const std::vector<quant::bt::Bar>& bars);

// Fundamental record: (report_date YYYY-MM-DD, value).
using FinancialSeries = std::vector<std::pair<std::string, double>>;

// Compute fundamental features (PE, ROE, gross margin, debt ratio) by forward-filling
// the latest report_date <= bar.date for each field.
// fin_data maps field name ("eps", "roe", "gross_margin", "debt_ratio") to its series.
std::map<std::string, std::vector<double>> calc_fundamental_features(
    const std::vector<quant::bt::Bar>& bars,
    const std::map<std::string, FinancialSeries>& fin_data);

} // namespace quant::ml
