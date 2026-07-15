#include "ml_tree.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <random>

namespace quant::ml {

DecisionTree::DecisionTree(bool regression, size_t max_depth, size_t min_samples_leaf)
    : regression_(regression), max_depth_(max_depth), min_samples_leaf_(min_samples_leaf) {}

void DecisionTree::set_feature_subset(const std::vector<size_t>& features) {
    feature_subset_ = features;
}

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

    size_t n_feat_candidates = feature_subset_.empty() ? n_features_ : feature_subset_.size();
    for (size_t fi = 0; fi < n_feat_candidates; ++fi) {
        size_t f = feature_subset_.empty() ? fi : feature_subset_[fi];
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

// ============================================================
// Random Forest Classifier
// ============================================================

RandomForestClassifier::RandomForestClassifier(size_t n_trees, size_t max_depth,
                                               size_t min_samples_leaf,
                                               size_t max_features, uint32_t seed)
    : n_trees_(n_trees), max_depth_(max_depth), min_samples_leaf_(min_samples_leaf),
      max_features_(max_features), seed_(seed) {}

void RandomForestClassifier::fit(const std::vector<std::vector<double>>& X,
                                 const std::vector<double>& y) {
    trees_.clear();
    tree_features_.clear();
    feature_importances_.clear();
    if (X.empty() || y.empty() || X.size() != y.size()) return;

    size_t n_samples = X.size();
    size_t n_features = X[0].size();
    size_t max_feat = max_features_ == 0 ? static_cast<size_t>(std::sqrt(n_features)) : max_features_;
    max_feat = std::min(max_feat, n_features);

    std::mt19937 rng(seed_);
    std::uniform_int_distribution<size_t> sample_dist(0, n_samples - 1);

    trees_.reserve(n_trees_);
    tree_features_.reserve(n_trees_);
    std::vector<std::vector<double>> importances(n_trees_, std::vector<double>(n_features, 0.0));

    for (size_t t = 0; t < n_trees_; ++t) {
        // Bootstrap sample
        std::vector<size_t> boot_idx(n_samples);
        for (size_t i = 0; i < n_samples; ++i) boot_idx[i] = sample_dist(rng);

        std::vector<std::vector<double>> Xb(n_samples, std::vector<double>(n_features));
        std::vector<double> yb(n_samples);
        for (size_t i = 0; i < n_samples; ++i) {
            Xb[i] = X[boot_idx[i]];
            yb[i] = y[boot_idx[i]];
        }

        // Random feature subset
        std::vector<size_t> all_features(n_features);
        std::iota(all_features.begin(), all_features.end(), 0);
        std::shuffle(all_features.begin(), all_features.end(), rng);
        std::vector<size_t> subset(all_features.begin(), all_features.begin() + max_feat);

        DecisionTree tree(false, max_depth_, min_samples_leaf_);
        tree.set_feature_subset(subset);
        tree.fit(Xb, yb);

        trees_.push_back(std::move(tree));
        tree_features_.push_back(subset);
        importances[t] = trees_.back().feature_importances();
    }

    feature_importances_.assign(n_features, 0.0);
    for (size_t f = 0; f < n_features; ++f) {
        for (size_t t = 0; t < n_trees_; ++t) feature_importances_[f] += importances[t][f];
        feature_importances_[f] /= static_cast<double>(n_trees_);
    }
    double mx = *std::max_element(feature_importances_.begin(), feature_importances_.end());
    if (mx > 0.0) {
        for (double& v : feature_importances_) v /= mx;
    }
}

int RandomForestClassifier::predict_class(const std::vector<double>& x) const {
    return predict_proba(x) > 0.5 ? 1 : 0;
}

double RandomForestClassifier::predict_proba(const std::vector<double>& x) const {
    if (trees_.empty()) return 0.0;
    double sum = 0.0;
    for (const auto& tree : trees_) sum += tree.predict(x);
    return sum / static_cast<double>(trees_.size());
}

std::vector<double> RandomForestClassifier::feature_importances() const {
    return feature_importances_;
}

// ============================================================
// Labels
// ============================================================

std::vector<int> make_binary_labels(const std::vector<double>& close, size_t horizon) {
    size_t n = close.size();
    std::vector<int> out(n, -1);
    if (horizon == 0) return out;
    for (size_t i = 0; i + horizon < n; ++i) {
        if (close[i] != 0.0) {
            double ret = close[i + horizon] / close[i] - 1.0;
            out[i] = ret > 0.0 ? 1 : 0;
        }
    }
    return out;
}

std::vector<int> make_ternary_labels(const std::vector<double>& close, size_t horizon,
                                     double threshold) {
    size_t n = close.size();
    std::vector<int> out(n, -1);
    if (horizon == 0) return out;
    for (size_t i = 0; i + horizon < n; ++i) {
        if (close[i] != 0.0) {
            double ret = close[i + horizon] / close[i] - 1.0;
            if (ret > threshold) out[i] = 2;
            else if (ret < -threshold) out[i] = 0;
            else out[i] = 1;
        }
    }
    return out;
}

// ============================================================
// Rolling train/predict
// ============================================================

std::vector<Prediction> rolling_train_predict(
    const std::vector<std::vector<double>>& X,
    const std::vector<int>& y,
    const std::vector<std::string>& dates,
    const RandomForestClassifier& prototype,
    size_t train_window,
    size_t retrain_interval) {
    std::vector<Prediction> out;
    size_t n = X.size();
    if (n == 0 || y.size() != n || dates.size() != n) return out;
    if (train_window == 0 || retrain_interval == 0) return out;

    RandomForestClassifier model = prototype;
    size_t last_train = static_cast<size_t>(-1);

    for (size_t i = train_window; i < n; ++i) {
        if (last_train == static_cast<size_t>(-1) || (i - last_train) >= retrain_interval) {
            size_t start = i - train_window;
            std::vector<std::vector<double>> Xtr(train_window);
            std::vector<double> ytr(train_window);
            for (size_t j = 0; j < train_window; ++j) {
                Xtr[j] = X[start + j];
                ytr[j] = static_cast<double>(y[start + j]);
            }
            // Check both classes present
            bool has0 = false, has1 = false;
            for (double yi : ytr) {
                if (yi == 0.0) has0 = true;
                if (yi == 1.0) has1 = true;
            }
            if (!has0 || !has1) continue;
            model = prototype;
            model.fit(Xtr, ytr);
            last_train = i;
        }
        Prediction p;
        p.date = dates[i];
        p.y_true = y[i];
        p.y_prob = model.predict_proba(X[i]);
        p.y_pred = p.y_prob > 0.5 ? 1 : 0;
        out.push_back(p);
    }
    return out;
}

// ============================================================
// Classification metrics
// ============================================================

ClfMetrics evaluate_classification(const std::vector<int>& y_true,
                                   const std::vector<int>& y_pred,
                                   const std::vector<double>& y_prob) {
    ClfMetrics m;
    size_t n = y_true.size();
    if (n == 0 || y_pred.size() != n) return m;

    size_t tp = 0, fp = 0, tn = 0, fn = 0;
    for (size_t i = 0; i < n; ++i) {
        if (y_true[i] == 1 && y_pred[i] == 1) ++tp;
        else if (y_true[i] == 0 && y_pred[i] == 1) ++fp;
        else if (y_true[i] == 0 && y_pred[i] == 0) ++tn;
        else if (y_true[i] == 1 && y_pred[i] == 0) ++fn;
    }

    m.accuracy = static_cast<double>(tp + tn) / static_cast<double>(n);
    m.precision = (tp + fp > 0) ? static_cast<double>(tp) / static_cast<double>(tp + fp) : 0.0;
    m.recall = (tp + fn > 0) ? static_cast<double>(tp) / static_cast<double>(tp + fn) : 0.0;
    if (m.precision + m.recall > 0.0) {
        m.f1 = 2.0 * m.precision * m.recall / (m.precision + m.recall);
    }

    if (y_prob.size() == n) {
        // Mann-Whitney U / AUC via probability that a random positive ranks above a random negative.
        std::vector<std::pair<double, int>> ranked;
        ranked.reserve(n);
        for (size_t i = 0; i < n; ++i) ranked.emplace_back(y_prob[i], y_true[i]);
        std::sort(ranked.begin(), ranked.end());

        size_t pos_count = 0, neg_count = 0;
        double u = 0.0;
        for (const auto& pr : ranked) {
            if (pr.second == 1) {
                ++pos_count;
                u += neg_count;
            } else {
                ++neg_count;
            }
        }
        if (pos_count > 0 && neg_count > 0) {
            m.auc = u / (static_cast<double>(pos_count) * static_cast<double>(neg_count));
        }
    }
    return m;
}

// ============================================================
// Purged K-Fold CV
// ============================================================

std::vector<CvFoldMetrics> purged_kfold_cv(
    const std::vector<std::vector<double>>& X,
    const std::vector<int>& y,
    const RandomForestClassifier& prototype,
    size_t n_splits, size_t gap) {
    std::vector<CvFoldMetrics> result;
    size_t n = X.size();
    if (n < n_splits * 2 || y.size() != n) return result;

    size_t fold_size = n / n_splits;
    for (size_t fold = 0; fold < n_splits; ++fold) {
        size_t val_start = fold * fold_size;
        size_t val_end = std::min(val_start + fold_size, n);
        size_t train_end = val_start > gap ? val_start - gap : 0;
        if (train_end < 30) continue;

        std::vector<std::vector<double>> Xtr(X.begin(), X.begin() + train_end);
        std::vector<double> ytr(y.begin(), y.begin() + train_end);
        std::vector<std::vector<double>> Xval(X.begin() + val_start, X.begin() + val_end);
        std::vector<int> yval(y.begin() + val_start, y.begin() + val_end);

        bool has0 = false, has1 = false;
        for (int yi : yval) { if (yi == 0) has0 = true; else if (yi == 1) has1 = true; }
        if (!has0 || !has1) continue;
        has0 = false; has1 = false;
        for (double yi : ytr) { if (yi == 0.0) has0 = true; else if (yi == 1.0) has1 = true; }
        if (!has0 || !has1) continue;

        RandomForestClassifier model = prototype;
        model.fit(Xtr, ytr);

        std::vector<int> ypred;
        std::vector<double> yprob;
        ypred.reserve(Xval.size());
        yprob.reserve(Xval.size());
        for (const auto& x : Xval) {
            double prob = model.predict_proba(x);
            yprob.push_back(prob);
            ypred.push_back(prob > 0.5 ? 1 : 0);
        }

        CvFoldMetrics fm;
        fm.fold = static_cast<int>(fold);
        fm.metrics = evaluate_classification(yval, ypred, yprob);
        result.push_back(fm);
    }
    return result;
}

// ============================================================
// Factor evaluation
// ============================================================

namespace {

double pearson(const std::vector<double>& a, const std::vector<double>& b) {
    size_t n = a.size();
    if (n == 0 || n != b.size()) return std::numeric_limits<double>::quiet_NaN();
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double num = 0.0, da = 0.0, db = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double xa = a[i] - ma, xb = b[i] - mb;
        num += xa * xb;
        da += xa * xa;
        db += xb * xb;
    }
    double den = std::sqrt(da * db);
    return den > 0.0 ? num / den : std::numeric_limits<double>::quiet_NaN();
}

double spearman(const std::vector<double>& a, const std::vector<double>& b) {
    size_t n = a.size();
    if (n == 0 || n != b.size()) return std::numeric_limits<double>::quiet_NaN();
    auto rank = [](const std::vector<double>& v) {
        std::vector<std::pair<double, size_t>> p;
        p.reserve(v.size());
        for (size_t i = 0; i < v.size(); ++i) p.emplace_back(v[i], i);
        std::sort(p.begin(), p.end());
        std::vector<double> r(v.size());
        size_t i = 0;
        while (i < p.size()) {
            size_t j = i;
            while (j < p.size() && p[j].first == p[i].first) ++j;
            double avg = (static_cast<double>(i) + 1.0 + static_cast<double>(j)) / 2.0;
            for (size_t k = i; k < j; ++k) r[p[k].second] = avg;
            i = j;
        }
        return r;
    };
    return pearson(rank(a), rank(b));
}

} // namespace

FactorMetrics evaluate_factor(const std::vector<std::string>& dates,
                              const std::vector<double>& probs,
                              const std::vector<double>& returns,
                              size_t n_groups) {
    FactorMetrics fm;
    size_t n = dates.size();
    if (n == 0 || probs.size() != n || returns.size() != n) return fm;

    std::map<std::string, std::vector<size_t>> groups;
    for (size_t i = 0; i < n; ++i) {
        if (!std::isnan(probs[i]) && !std::isnan(returns[i])) {
            groups[dates[i]].push_back(i);
        }
    }

    std::vector<double> daily_ic, daily_rank_ic;
    fm.quintile_returns.assign(n_groups, 0.0);
    size_t valid_days = 0;

    for (const auto& kv : groups) {
        const auto& idx = kv.second;
        if (idx.size() < 2) continue;
        std::vector<double> p, r;
        p.reserve(idx.size());
        r.reserve(idx.size());
        for (size_t i : idx) { p.push_back(probs[i]); r.push_back(returns[i]); }

        double ic = pearson(p, r);
        double ric = spearman(p, r);
        if (!std::isnan(ic)) daily_ic.push_back(ic);
        if (!std::isnan(ric)) daily_rank_ic.push_back(ric);

        // Quintile returns for this date
        std::vector<std::pair<double, double>> pr;
        pr.reserve(idx.size());
        for (size_t t = 0; t < idx.size(); ++t) pr.emplace_back(p[t], r[t]);
        std::sort(pr.begin(), pr.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

        size_t gsize = idx.size() / n_groups;
        size_t rem = idx.size() % n_groups;
        size_t pos = 0;
        std::vector<double> day_q(n_groups, 0.0);
        std::vector<size_t> day_cnt(n_groups, 0);
        for (size_t g = 0; g < n_groups; ++g) {
            size_t cnt = gsize + (g < rem ? 1 : 0);
            for (size_t t = 0; t < cnt && pos < pr.size(); ++t, ++pos) {
                day_q[g] += pr[pos].second;
                ++day_cnt[g];
            }
        }
        for (size_t g = 0; g < n_groups; ++g) {
            if (day_cnt[g] > 0) {
                fm.quintile_returns[g] += day_q[g] / static_cast<double>(day_cnt[g]);
            }
        }
        ++valid_days;
    }

    if (valid_days > 0) {
        for (double& v : fm.quintile_returns) v /= static_cast<double>(valid_days);
    }

    auto mean_vec = [](const std::vector<double>& v) -> double {
        if (v.empty()) return 0.0;
        return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    };
    auto std_vec = [](const std::vector<double>& v, double mean) -> double {
        if (v.size() < 2) return 0.0;
        double s = 0.0;
        for (double x : v) { double d = x - mean; s += d * d; }
        return std::sqrt(s / static_cast<double>(v.size() - 1));
    };

    fm.ic = mean_vec(daily_ic);
    fm.rank_ic = mean_vec(daily_rank_ic);
    fm.icir = (std_vec(daily_ic, fm.ic) > 0.0) ? fm.ic / std_vec(daily_ic, fm.ic) : 0.0;
    fm.rank_icir = (std_vec(daily_rank_ic, fm.rank_ic) > 0.0) ? fm.rank_ic / std_vec(daily_rank_ic, fm.rank_ic) : 0.0;

    if (!daily_ic.empty()) {
        size_t pos = 0;
        for (double x : daily_ic) if (x > 0.0) ++pos;
        fm.ic_positive_rate = static_cast<double>(pos) / static_cast<double>(daily_ic.size());
    }

    return fm;
}

} // namespace quant::ml
