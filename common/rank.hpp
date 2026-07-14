#pragma once

#include <optional>
#include <string>
#include <vector>

namespace quant::rank {

// 按 group 计算 values 的百分位排名（0~1），higher_better=true 表示越大越好。
// 缺失值或组内有效样本少于 2 时返回 NaN。
std::vector<double> group_rank_pct(const std::vector<std::string>& groups,
                                   const std::vector<std::optional<double>>& values,
                                   bool higher_better = true);

// 将百分位排名映射为 1~5 分（对应 pandas 脚本中的 int(x*5)+1 并截断）。
std::vector<std::optional<int>> pct_to_score(const std::vector<double>& pct);

} // namespace quant::rank
