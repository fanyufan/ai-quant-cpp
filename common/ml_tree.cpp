#include "ml_tree.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace quant::ml {

DecisionTree::DecisionTree(bool regression, size_t max_depth, size_t min_samples_leaf)
    : regression_(regression), max_depth_(max_depth), min_samples_leaf_(min_samples_leaf) {}

namespace {

double mean(const std::vector<double>& y, const std::vector<size_t>& idx) {
    double s = 0.0;
    for (size_t i : idx) s += y[i];
    return idx.empty() ? 0.0 : s / static_cast<double>(idx.size());
}

double mse(const std::vector<double>& y, const std::vector<size_t>& idx) {
    if (idx.empty()) return 0.0;
    double mu = mean(y, idx);
    double s = 0.0;
    for (size_t i : idx) {
        double d = y[i] - mu;
        s += d * d;
    }
    return s / static_cast<double>(idx.size());
}

double gini(const std::vector<double>& y, const std::vector<size_t>& idx) {
    if (idx.empty()) return 0.0;
    size_t pos = 0;
    for (size_t i : idx) if (y[i] >= 0.5) ++pos;
    double q = static_cast<double>(pos) / static_cast<double>(idx.size());
    return 1.0 - q * q - (1.0 - q) * (1.0 - q);
}

} // namespace

void DecisionTree::fit(const std::vector<std::vector<double>>& X,
                       const std::vector<double>& y) {
    nodes_.clear();
    if (X.empty()) return;
    n_features_ = X[0].size();
    std::vector<size_t> idx(X.size());
    std::iota(idx.begin(), idx.end(), 0);
    build(X, y, idx, 0);
}

double DecisionTree::predict(const std::vector<double>& x) const {
    if (nodes_.empty()) return 0.0;
    int node = 0;
    while (!nodes_[node].is_leaf) {
        if (x[nodes_[node].feature] <= nodes_[node].threshold) {
            node = nodes_[node].left;
        } else {
            node = nodes_[node].right;
        }
    }
    return nodes_[node].value;
}

std::vector<double> DecisionTree::feature_importances() const {
    std::vector<double> imp(n_features_, 0.0);
    for (const auto& n : nodes_) {
        if (!n.is_leaf) imp[n.feature] += 1.0;
    }
    double mx = *std::max_element(imp.begin(), imp.end());
    if (mx > 0.0) {
        for (double& v : imp) v /= mx;
    }
    return imp;
}

int DecisionTree::build(const std::vector<std::vector<double>>& X,
                        const std::vector<double>& y,
                        std::vector<size_t>& idx,
                        size_t depth) {
    Node node;
    node.value = mean(y, idx);

    size_t pos = 0;
    for (size_t i : idx) if (y[i] >= 0.5) ++pos;

    bool stop = false;
    if (depth >= max_depth_ || idx.size() <= min_samples_leaf_ * 2) stop = true;
    if (!regression_ && (pos == 0 || pos == idx.size())) stop = true;
    if (stop) {
        node.is_leaf = true;
        nodes_.push_back(node);
        return static_cast<int>(nodes_.size() - 1);
    }

    double parent_score = regression_ ? mse(y, idx) : gini(y, idx);
    double best_gain = -1.0;
    size_t best_feat = 0;
    double best_thr = 0.0;

    for (size_t f = 0; f < n_features_; ++f) {
        std::vector<std::pair<double, size_t>> vals;
        vals.reserve(idx.size());
        for (size_t i : idx) vals.emplace_back(X[i][f], i);
        std::sort(vals.begin(), vals.end());
        for (size_t k = 1; k < vals.size(); ++k) {
            if (vals[k].first == vals[k - 1].first) continue;
            double thr = (vals[k - 1].first + vals[k].first) / 2.0;
            size_t left_n = k;
            size_t right_n = vals.size() - k;
            if (left_n < min_samples_leaf_ || right_n < min_samples_leaf_) continue;
            std::vector<size_t> left_idx, right_idx;
            left_idx.reserve(left_n);
            right_idx.reserve(right_n);
            for (size_t t = 0; t < k; ++t) left_idx.push_back(vals[t].second);
            for (size_t t = k; t < vals.size(); ++t) right_idx.push_back(vals[t].second);
            double left_score = regression_ ? mse(y, left_idx) : gini(y, left_idx);
            double right_score = regression_ ? mse(y, right_idx) : gini(y, right_idx);
            double gain = parent_score - (left_n * left_score + right_n * right_score) / static_cast<double>(idx.size());
            if (gain > best_gain) {
                best_gain = gain;
                best_feat = f;
                best_thr = thr;
            }
        }
    }

    if (best_gain <= 0.0) {
        node.is_leaf = true;
        nodes_.push_back(node);
        return static_cast<int>(nodes_.size() - 1);
    }

    node.is_leaf = false;
    node.feature = best_feat;
    node.threshold = best_thr;
    int cur = static_cast<int>(nodes_.size());
    nodes_.push_back(node);

    std::vector<size_t> li, ri;
    li.reserve(idx.size());
    ri.reserve(idx.size());
    for (size_t i : idx) {
        if (X[i][best_feat] <= best_thr) li.push_back(i);
        else ri.push_back(i);
    }
    nodes_[cur].left = build(X, y, li, depth + 1);
    nodes_[cur].right = build(X, y, ri, depth + 1);
    return cur;
}

} // namespace quant::ml
