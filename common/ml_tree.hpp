#pragma once

#include <cstddef>
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
    std::vector<Node> nodes_;

    int build(const std::vector<std::vector<double>>& X,
              const std::vector<double>& y,
              std::vector<size_t>& idx,
              size_t depth);
};

} // namespace quant::ml
