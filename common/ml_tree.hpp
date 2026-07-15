#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace quant::ml {

class DecisionTree {
public:
    // regression=true -> MSE split, leaf = mean(y)
    // regression=false -> Gini split, leaf = P(class=1)
    DecisionTree(bool regression, size_t max_depth, size_t min_samples_leaf);

    void fit(const std::vector<std::vector<double>>& X,
             const std::vector<double>& y);

    double predict(const std::vector<double>& x) const;

    std::vector<double> feature_importances() const;

    // Optional subset of feature indices to consider during training.
    // When empty (default), all features are used.
    void set_feature_subset(const std::vector<size_t>& features);

    struct Node {
        bool is_leaf = true;
        size_t feature = 0;
        double threshold = 0.0;
        double value = 0.0;
        int left = -1;
        int right = -1;
    };
    const std::vector<Node>& nodes() const { return nodes_; }

private:
    bool regression_;
    size_t max_depth_;
    size_t min_samples_leaf_;
    size_t n_features_ = 0;
    std::vector<size_t> feature_subset_;
    std::vector<Node> nodes_;

    int build(const std::vector<std::vector<double>>& X,
              const std::vector<double>& y,
              std::vector<size_t>& idx,
              size_t depth);
};

class RandomForestClassifier {
public:
    RandomForestClassifier(size_t n_trees, size_t max_depth, size_t min_samples_leaf,
                           size_t max_features = 0, uint32_t seed = 42);

    void fit(const std::vector<std::vector<double>>& X, const std::vector<double>& y);

    int predict_class(const std::vector<double>& x) const;
    double predict_proba(const std::vector<double>& x) const; // P(class=1)

    std::vector<double> feature_importances() const;

private:
    size_t n_trees_;
    size_t max_depth_;
    size_t min_samples_leaf_;
    size_t max_features_;
    uint32_t seed_;
    std::vector<DecisionTree> trees_;
    std::vector<std::vector<size_t>> tree_features_;
    std::vector<double> feature_importances_;
};

// Label construction.
std::vector<int> make_binary_labels(const std::vector<double>& close, size_t horizon);
std::vector<int> make_ternary_labels(const std::vector<double>& close, size_t horizon,
                                     double threshold = 0.02);

// Rolling walk-forward prediction.
struct Prediction {
    std::string date;
    int y_true = -1;
    int y_pred = -1;
    double y_prob = 0.0;
};
std::vector<Prediction> rolling_train_predict(
    const std::vector<std::vector<double>>& X,
    const std::vector<int>& y,
    const std::vector<std::string>& dates,
    const RandomForestClassifier& prototype,
    size_t train_window,
    size_t retrain_interval);

// Classification metrics.
struct ClfMetrics {
    double accuracy = 0.0;
    double precision = 0.0;
    double recall = 0.0;
    double f1 = 0.0;
    double auc = 0.0;
};
ClfMetrics evaluate_classification(const std::vector<int>& y_true,
                                   const std::vector<int>& y_pred,
                                   const std::vector<double>& y_prob);

// Purged K-Fold time-series CV.
struct CvFoldMetrics {
    int fold = 0;
    ClfMetrics metrics;
};
std::vector<CvFoldMetrics> purged_kfold_cv(
    const std::vector<std::vector<double>>& X,
    const std::vector<int>& y,
    const RandomForestClassifier& prototype,
    size_t n_splits = 5, size_t gap = 5);

// Factor evaluation: IC / RankIC / quintile returns.
struct FactorMetrics {
    double ic = 0.0;
    double icir = 0.0;
    double rank_ic = 0.0;
    double rank_icir = 0.0;
    double ic_positive_rate = 0.0;
    std::vector<double> quintile_returns;
};
FactorMetrics evaluate_factor(const std::vector<std::string>& dates,
                              const std::vector<double>& probs,
                              const std::vector<double>& returns,
                              size_t n_groups = 5);

} // namespace quant::ml
