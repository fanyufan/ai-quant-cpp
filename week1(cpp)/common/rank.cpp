#include "rank.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace quant::rank {

std::vector<double> group_rank_pct(const std::vector<std::string>& groups,
                                   const std::vector<std::optional<double>>& values,
                                   bool higher_better) {
    const size_t n = groups.size();
    const double NaN = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> result(n, NaN);

    std::unordered_map<std::string, std::vector<size_t>> idx_by_group;
    idx_by_group.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        idx_by_group[groups[i]].push_back(i);
    }

    struct Item {
        double value = 0.0;
        size_t orig = 0;
    };

    for (auto& [group, idxs] : idx_by_group) {
        std::vector<Item> items;
        items.reserve(idxs.size());
        for (size_t i : idxs) {
            if (values[i].has_value()) {
                items.push_back({*values[i], i});
            }
        }

        const size_t m = items.size();
        if (m < 2) {
            for (const auto& it : items) {
                result[it.orig] = NaN;
            }
            continue;
        }

        std::sort(items.begin(), items.end(),
                  [](const Item& a, const Item& b) { return a.value < b.value; });

        size_t pos = 0;
        while (pos < m) {
            size_t end = pos;
            while (end < m && items[end].value == items[pos].value) {
                ++end;
            }
            // ranks are 1-based positions [pos+1, end]
            const double avg_rank = (static_cast<double>(pos) + 1.0 + static_cast<double>(end)) / 2.0;
            double pct = (avg_rank - 1.0) / (static_cast<double>(m) - 1.0);
            if (!higher_better) {
                pct = 1.0 - pct;
            }
            for (size_t k = pos; k < end; ++k) {
                result[items[k].orig] = pct;
            }
            pos = end;
        }
    }

    return result;
}

std::vector<std::optional<int>> pct_to_score(const std::vector<double>& pct) {
    std::vector<std::optional<int>> result;
    result.reserve(pct.size());
    for (double p : pct) {
        if (std::isnan(p)) {
            result.emplace_back();
            continue;
        }
        int s = static_cast<int>(p * 5.0) + 1;
        if (s < 1) s = 1;
        if (s > 5) s = 5;
        result.emplace_back(s);
    }
    return result;
}

} // namespace quant::rank
