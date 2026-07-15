#pragma once

#include <map>
#include <string>
#include <vector>

namespace quant::ml {

// NaN-aware scalar helpers.
double nanmedian(const std::vector<double>& x);
double nanmean(const std::vector<double>& x);
double nanstd(const std::vector<double>& x);

// Outlier clipping / scaling (NaN-aware, non-NaN values only).
std::vector<double> clip_by_mad(const std::vector<double>& x, double n_mad = 5.0);
std::vector<double> clip_by_sigma(const std::vector<double>& x, double n_sigma = 3.0);
std::vector<double> zscore(const std::vector<double>& x);
std::vector<double> robust_zscore_norm(const std::vector<double>& x, double clip_range = 3.0);

void fill_nan_with(std::vector<double>& x, double value);
void fill_nan_with_median(std::vector<double>& x);

// Panel row for cross-sectional / multi-stock processing.
struct PanelRow {
    std::string date;
    std::string code;
    double close = 0;
    std::map<std::string, double> features;   // feature name -> value
    std::map<std::string, double> industries; // industry dummy name -> 0/1
    double mktcap_log = 0;
};

// Per-stock preprocessing: MAD clip -> fill median -> z-score.
std::vector<PanelRow> preprocess_panel(const std::vector<PanelRow>& panel,
                                       const std::vector<std::string>& feature_cols,
                                       const std::string& method = "mad");

// Cross-sectional preprocessing: for each date, MAD clip -> fill median -> z-score.
std::vector<PanelRow> preprocess_cross_section(const std::vector<PanelRow>& panel,
                                               const std::vector<std::string>& feature_cols);

// Industry / market-cap neutralization via OLS residual.
std::vector<PanelRow> neutralize_factor(std::vector<PanelRow> panel,
                                        const std::string& factor,
                                        const std::vector<std::string>& ind_cols,
                                        bool use_mktcap = true);

} // namespace quant::ml
