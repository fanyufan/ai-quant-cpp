#pragma once

// 通用连续状态离散化工具（用于 CartPole 等 Q-learning 环境）

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace quant::rl {

class Discretizer {
public:
    // bins:  每个维度要划分成的区间数
    // lows / highs: 每个维度的下界/上界
    Discretizer(const std::vector<size_t>& bins,
                const std::vector<double>& lows,
                const std::vector<double>& highs)
        : bins_(bins), lows_(lows), highs_(highs) {
        if (bins_.size() != lows_.size() || bins_.size() != highs_.size()) {
            throw std::invalid_argument("Discretizer: bins/lows/highs size mismatch");
        }
        for (size_t i = 0; i < bins_.size(); ++i) {
            if (bins_[i] == 0) bins_[i] = 1;
            if (lows_[i] >= highs_[i]) {
                // 退化为单一区间
                highs_[i] = lows_[i] + 1.0;
            }
        }
    }

    // 将观测值向量映射为唯一状态 ID（混合进制）
    size_t state(const std::vector<double>& obs) const {
        if (obs.size() != bins_.size()) {
            throw std::invalid_argument("Discretizer: observation dimension mismatch");
        }
        size_t id = 0;
        size_t multiplier = 1;
        for (size_t d = 0; d < bins_.size(); ++d) {
            size_t idx = discretize(obs[d], d);
            id += idx * multiplier;
            multiplier *= bins_[d];
        }
        return id;
    }

    size_t state_count() const {
        size_t total = 1;
        for (size_t b : bins_) total *= b;
        return total;
    }

    const std::vector<size_t>& bins() const { return bins_; }
    const std::vector<double>& lows() const { return lows_; }
    const std::vector<double>& highs() const { return highs_; }

private:
    size_t discretize(double value, size_t dim) const {
        double low = lows_[dim];
        double high = highs_[dim];
        size_t n = bins_[dim];
        if (value <= low) return 0;
        if (value >= high) return n - 1;
        double interval = (high - low) / static_cast<double>(n);
        size_t idx = static_cast<size_t>((value - low) / interval);
        if (idx >= n) idx = n - 1;
        return idx;
    }

    std::vector<size_t> bins_;
    std::vector<double> lows_;
    std::vector<double> highs_;
};

} // namespace quant::rl
