#pragma once

// 通用 Q-learning 工具：Q-table、epsilon-greedy 动作选择

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <unordered_map>
#include <vector>

namespace quant::rl {

class QTable {
public:
    explicit QTable(size_t n_actions, double init_mean = 0.0, double init_std = 0.0)
        : n_actions_(n_actions), init_mean_(init_mean), init_std_(init_std), rng_(std::random_device{}()) {}

    size_t n_actions() const { return n_actions_; }

    // 获取某状态的 Q 值向量（不存在则按高斯初始化）
    std::vector<double>& values(size_t state) {
        auto it = table_.find(state);
        if (it == table_.end()) {
            std::vector<double> v(n_actions_);
            if (init_std_ > 0.0) {
                std::normal_distribution<double> dist(init_mean_, init_std_);
                for (double& x : v) x = dist(rng_);
            } else {
                std::fill(v.begin(), v.end(), init_mean_);
            }
            auto emplace_it = table_.emplace(state, std::move(v)).first;
            return emplace_it->second;
        }
        return it->second;
    }

    const std::vector<double>& values(size_t state) const {
        auto it = table_.find(state);
        if (it == table_.end()) {
            static const std::vector<double> zeros;
            return zeros;
        }
        return it->second;
    }

    size_t best_action(size_t state) const {
        const auto& v = values(state);
        if (v.empty()) return 0;
        return static_cast<size_t>(std::max_element(v.begin(), v.end()) - v.begin());
    }

    size_t size() const { return table_.size(); }

private:
    size_t n_actions_;
    double init_mean_;
    double init_std_;
    mutable std::mt19937 rng_;
    std::unordered_map<size_t, std::vector<double>> table_;
};

class EpsilonGreedyAgent {
public:
    EpsilonGreedyAgent(QTable& q, double epsilon)
        : q_(q), epsilon_(epsilon), rng_(std::random_device{}()) {}

    size_t act(size_t state) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(rng_) < epsilon_) {
            std::uniform_int_distribution<size_t> action_dist(0, q_.n_actions() - 1);
            return action_dist(rng_);
        }
        return q_.best_action(state);
    }

    void set_epsilon(double e) { epsilon_ = e; }
    double epsilon() const { return epsilon_; }

private:
    QTable& q_;
    double epsilon_;
    std::mt19937 rng_;
};

} // namespace quant::rl
