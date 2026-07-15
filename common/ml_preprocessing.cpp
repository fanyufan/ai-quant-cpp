#include "ml_preprocessing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <vector>

namespace quant::ml {

namespace {

const double NaN = std::numeric_limits<double>::quiet_NaN();

std::vector<double> collect_valid(const std::vector<double>& x) {
    std::vector<double> v;
    v.reserve(x.size());
    for (double xi : x) {
        if (!std::isnan(xi)) v.push_back(xi);
    }
    return v;
}

double median_sorted(std::vector<double> v) {
    if (v.empty()) return NaN;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n % 2 == 1) return v[n / 2];
    return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

// Solve A * beta = b for beta using Gaussian elimination with partial pivoting.
// A is assumed square (p x p), b length p. Returns empty on failure.
std::vector<double> solve_linear_system(std::vector<std::vector<double>> A,
                                        std::vector<double> b) {
    size_t p = A.size();
    if (p == 0 || b.size() != p) return {};
    const double eps = 1e-12;

    for (size_t i = 0; i < p; ++i) {
        // Partial pivot
        size_t pivot = i;
        double max_val = std::abs(A[i][i]);
        for (size_t r = i + 1; r < p; ++r) {
            double val = std::abs(A[r][i]);
            if (val > max_val) {
                max_val = val;
                pivot = r;
            }
        }
        if (max_val < eps) return {}; // singular
        if (pivot != i) {
            std::swap(A[pivot], A[i]);
            std::swap(b[pivot], b[i]);
        }

        // Eliminate rows below
        for (size_t r = i + 1; r < p; ++r) {
            double factor = A[r][i] / A[i][i];
            for (size_t c = i; c < p; ++c) {
                A[r][c] -= factor * A[i][c];
            }
            b[r] -= factor * b[i];
        }
    }

    // Back substitution
    std::vector<double> beta(p, 0.0);
    for (int i = static_cast<int>(p) - 1; i >= 0; --i) {
        double sum = b[i];
        for (size_t c = i + 1; c < p; ++c) sum -= A[i][c] * beta[c];
        if (std::abs(A[i][i]) < eps) return {};
        beta[i] = sum / A[i][i];
    }
    return beta;
}

// Compute OLS residual y - X*beta. X is n x p, y length n.
std::vector<double> linear_regression_residuals(const std::vector<std::vector<double>>& X,
                                                const std::vector<double>& y) {
    size_t n = X.size();
    if (n == 0 || y.size() != n || X[0].empty()) return std::vector<double>(n, NaN);
    size_t p = X[0].size();

    // X^T X and X^T y
    std::vector<std::vector<double>> XtX(p, std::vector<double>(p, 0.0));
    std::vector<double> Xty(p, 0.0);
    for (size_t i = 0; i < n; ++i) {
        for (size_t c = 0; c < p; ++c) {
            Xty[c] += X[i][c] * y[i];
            for (size_t d = 0; d < p; ++d) {
                XtX[c][d] += X[i][c] * X[i][d];
            }
        }
    }

    // Small ridge for stability
    for (size_t i = 0; i < p; ++i) XtX[i][i] += 1e-8;

    auto beta = solve_linear_system(XtX, Xty);
    if (beta.empty()) return std::vector<double>(n, NaN);

    std::vector<double> residual(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        double pred = 0.0;
        for (size_t c = 0; c < p; ++c) pred += X[i][c] * beta[c];
        residual[i] = y[i] - pred;
    }
    return residual;
}

void process_series(std::vector<double>& series, const std::string& method) {
    auto valid = collect_valid(series);
    if (valid.empty()) return;

    double med = median_sorted(valid);
    if (method == "mad") {
        std::vector<double> abs_dev;
        abs_dev.reserve(valid.size());
        for (double v : valid) abs_dev.push_back(std::abs(v - med));
        double mad = median_sorted(abs_dev);
        double range = 5.0 * 1.4826 * mad;
        if (range > 0.0 && !std::isnan(range)) {
            double lower = med - range;
            double upper = med + range;
            for (double& x : series) {
                if (!std::isnan(x)) x = std::max(lower, std::min(upper, x));
            }
        }
    } else if (method == "sigma") {
        double mean_val = nanmean(series);
        double std_val = nanstd(series);
        if (std_val > 0.0 && !std::isnan(std_val)) {
            double lower = mean_val - 3.0 * std_val;
            double upper = mean_val + 3.0 * std_val;
            for (double& x : series) {
                if (!std::isnan(x)) x = std::max(lower, std::min(upper, x));
            }
        }
    }

    fill_nan_with_median(series);

    double mean_val = nanmean(series);
    double std_val = nanstd(series);
    if (std_val > 0.0 && !std::isnan(std_val)) {
        for (double& x : series) x = (x - mean_val) / std_val;
    } else {
        for (double& x : series) x = x - mean_val;
    }
}

} // namespace

double nanmedian(const std::vector<double>& x) {
    auto v = collect_valid(x);
    return median_sorted(v);
}

double nanmean(const std::vector<double>& x) {
    double sum = 0.0;
    size_t cnt = 0;
    for (double xi : x) {
        if (!std::isnan(xi)) {
            sum += xi;
            ++cnt;
        }
    }
    return cnt == 0 ? NaN : sum / static_cast<double>(cnt);
}

double nanstd(const std::vector<double>& x) {
    auto v = collect_valid(x);
    if (v.size() < 2) return NaN;
    double mean = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    double sq = 0.0;
    for (double xi : v) {
        double d = xi - mean;
        sq += d * d;
    }
    // Sample standard deviation (ddof=1), matching pandas default.
    return std::sqrt(sq / static_cast<double>(v.size() - 1));
}

std::vector<double> clip_by_mad(const std::vector<double>& x, double n_mad) {
    auto out = x;
    double med = nanmedian(out);
    auto valid = collect_valid(out);
    std::vector<double> abs_dev;
    abs_dev.reserve(valid.size());
    for (double v : valid) abs_dev.push_back(std::abs(v - med));
    double mad = median_sorted(abs_dev);
    double range = n_mad * 1.4826 * mad;
    if (range <= 0.0 || std::isnan(range)) return out;
    double lower = med - range;
    double upper = med + range;
    for (double& v : out) {
        if (!std::isnan(v)) v = std::max(lower, std::min(upper, v));
    }
    return out;
}

std::vector<double> clip_by_sigma(const std::vector<double>& x, double n_sigma) {
    auto out = x;
    double mean_val = nanmean(out);
    double std_val = nanstd(out);
    if (std_val <= 0.0 || std::isnan(std_val)) return out;
    double lower = mean_val - n_sigma * std_val;
    double upper = mean_val + n_sigma * std_val;
    for (double& v : out) {
        if (!std::isnan(v)) v = std::max(lower, std::min(upper, v));
    }
    return out;
}

std::vector<double> zscore(const std::vector<double>& x) {
    auto out = x;
    double mean_val = nanmean(out);
    double std_val = nanstd(out);
    if (std_val <= 0.0 || std::isnan(std_val)) {
        for (double& v : out) if (!std::isnan(v)) v = v - mean_val;
        return out;
    }
    for (double& v : out) {
        if (!std::isnan(v)) v = (v - mean_val) / std_val;
    }
    return out;
}

std::vector<double> robust_zscore_norm(const std::vector<double>& x, double clip_range) {
    auto out = x;
    double med = nanmedian(out);
    auto valid = collect_valid(out);
    std::vector<double> abs_dev;
    abs_dev.reserve(valid.size());
    for (double v : valid) abs_dev.push_back(std::abs(v - med));
    double mad = median_sorted(abs_dev);
    double scale = 1.4826 * mad;
    if (scale <= 0.0 || std::isnan(scale)) return out;
    for (double& v : out) {
        if (!std::isnan(v)) {
            double z = (v - med) / scale;
            v = std::max(-clip_range, std::min(clip_range, z));
        }
    }
    return out;
}

void fill_nan_with(std::vector<double>& x, double value) {
    for (double& v : x) {
        if (std::isnan(v)) v = value;
    }
}

void fill_nan_with_median(std::vector<double>& x) {
    double med = nanmedian(x);
    if (std::isnan(med)) return;
    fill_nan_with(x, med);
}

std::vector<PanelRow> preprocess_panel(const std::vector<PanelRow>& panel,
                                       const std::vector<std::string>& feature_cols,
                                       const std::string& method) {
    std::map<std::string, std::vector<size_t>> groups;
    for (size_t i = 0; i < panel.size(); ++i) {
        groups[panel[i].code].push_back(i);
    }

    std::vector<PanelRow> out = panel;
    for (const auto& fc : feature_cols) {
        for (const auto& kv : groups) {
            std::vector<double> series;
            series.reserve(kv.second.size());
            for (size_t idx : kv.second) {
                auto it = out[idx].features.find(fc);
                series.push_back(it != out[idx].features.end() ? it->second : NaN);
            }
            process_series(series, method);
            for (size_t t = 0; t < kv.second.size(); ++t) {
                out[kv.second[t]].features[fc] = series[t];
            }
        }
    }
    return out;
}

std::vector<PanelRow> preprocess_cross_section(const std::vector<PanelRow>& panel,
                                               const std::vector<std::string>& feature_cols) {
    std::map<std::string, std::vector<size_t>> groups;
    for (size_t i = 0; i < panel.size(); ++i) {
        groups[panel[i].date].push_back(i);
    }

    std::vector<PanelRow> out = panel;
    for (const auto& fc : feature_cols) {
        for (const auto& kv : groups) {
            std::vector<double> series;
            series.reserve(kv.second.size());
            for (size_t idx : kv.second) {
                auto it = out[idx].features.find(fc);
                series.push_back(it != out[idx].features.end() ? it->second : NaN);
            }
            process_series(series, "mad");
            for (size_t t = 0; t < kv.second.size(); ++t) {
                out[kv.second[t]].features[fc] = series[t];
            }
        }
    }
    return out;
}

std::vector<PanelRow> neutralize_factor(std::vector<PanelRow> panel,
                                        const std::string& factor,
                                        const std::vector<std::string>& ind_cols,
                                        bool use_mktcap) {
    size_t n = panel.size();
    if (n == 0) return panel;

    // Build matrix only for rows with valid factor.
    std::vector<size_t> valid_idx;
    std::vector<double> y;
    std::vector<std::vector<double>> X;
    valid_idx.reserve(n);
    y.reserve(n);
    X.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        auto it = panel[i].features.find(factor);
        if (it == panel[i].features.end() || std::isnan(it->second)) continue;

        std::vector<double> row;
        row.reserve(ind_cols.size() + (use_mktcap ? 1 : 0));
        for (const auto& col : ind_cols) {
            auto iit = panel[i].industries.find(col);
            row.push_back(iit != panel[i].industries.end() ? iit->second : 0.0);
        }
        if (use_mktcap) row.push_back(panel[i].mktcap_log);

        valid_idx.push_back(i);
        y.push_back(it->second);
        X.push_back(std::move(row));
    }

    if (valid_idx.size() < 10 || X.empty() || X[0].empty()) return panel;

    auto residual = linear_regression_residuals(X, y);
    for (size_t t = 0; t < valid_idx.size(); ++t) {
        panel[valid_idx[t]].features[factor] = residual[t];
    }
    return panel;
}

} // namespace quant::ml
