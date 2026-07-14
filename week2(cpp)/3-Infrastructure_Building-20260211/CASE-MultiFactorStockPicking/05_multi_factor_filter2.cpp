// 对应 Python: 多因子选股-筛选2.py
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "csv.hpp"
#include "plotter.hpp"
#include "rank.hpp"

namespace fs = std::filesystem;

static std::optional<double> parse_double(const std::string& s) {
    if (s.empty()) return std::nullopt;
    try {
        size_t pos = 0;
        double v = std::stod(s, &pos);
        if (pos != s.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

static std::string format_double(double v, int prec = 2) {
    return fmt::format("{:.{}}f", v, prec);
}

static double median_sorted(const std::vector<double>& v) {
    if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::vector<double> copy = v;
    std::sort(copy.begin(), copy.end());
    size_t n = copy.size();
    if (n % 2 == 1) return copy[n / 2];
    return (copy[n / 2 - 1] + copy[n / 2]) / 2.0;
}

int main(int argc, char* argv[]) {
    const int SCORE_MIN = 18;
    const bool ENABLE_VISUALIZATION = true;

    std::string input_file = "data/stock_fina_pool_QMT.csv";
    std::string output_file = "data/stock_fina_selected_QMT_industry.csv";
    std::string viz_dir = "data/industry_viz";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--input" || arg == "-i") && i + 1 < argc) {
            input_file = argv[++i];
        } else if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            output_file = argv[++i];
        } else if ((arg == "--viz-dir") && i + 1 < argc) {
            viz_dir = argv[++i];
        }
    }

    fmt::print("多因子选股 - 筛选2（行业版，打分制）\n");
    fmt::print("筛选条件：5 项各 1~5 分（按行业内排名），总分 >= {}\n", SCORE_MIN);
    fmt::print("{:-<60}\n", "");

    if (!fs::exists(input_file)) {
        fmt::print("错误：未找到输入文件 {}\n", input_file);
        fmt::print("请先运行数据下载脚本或指定 --input 路径\n");
        return 1;
    }

    auto data = quant::csv::read_csv(input_file);
    fmt::print("读取 {} 只股票\n", data.nrow());

    auto col_opt = [&](const std::string& name) -> std::optional<size_t> {
        return data.col_index(name);
    };

    auto get_val = [&](size_t row, const std::string& name) -> std::optional<double> {
        auto idx = col_opt(name);
        if (!idx) return std::nullopt;
        return parse_double(data.rows[row][*idx]);
    };

    // 行业列
    std::vector<std::string> groups(data.nrow());
    auto industry_opt = col_opt("industry");
    for (size_t i = 0; i < data.nrow(); ++i) {
        if (industry_opt) {
            std::string ind = data.rows[i][*industry_opt];
            groups[i] = ind;
        }
    }

    // 计算各行业百分位排名及得分
    std::vector<std::string> metric_cols = {
        "roe", "netprofit_yoy", "grossprofit_margin", "debt_to_assets", "ocf_to_revenue"
    };
    std::vector<bool> higher_better = {true, true, true, false, true};

    std::vector<std::vector<std::optional<double>>> pct_values(metric_cols.size(),
                                                                std::vector<std::optional<double>>(data.nrow()));
    std::vector<std::vector<std::optional<int>>> score_values(metric_cols.size(),
                                                               std::vector<std::optional<int>>(data.nrow()));

    for (size_t m = 0; m < metric_cols.size(); ++m) {
        const auto& col = metric_cols[m];
        auto idx = col_opt(col);
        if (!idx) continue;

        std::vector<std::optional<double>> values(data.nrow());
        for (size_t i = 0; i < data.nrow(); ++i) {
            values[i] = parse_double(data.rows[i][*idx]);
        }

        auto pct = quant::rank::group_rank_pct(groups, values, higher_better[m]);
        auto scores = quant::rank::pct_to_score(pct);
        for (size_t i = 0; i < data.nrow(); ++i) {
            if (!std::isnan(pct[i])) pct_values[m][i] = pct[i];
            score_values[m][i] = scores[i];
        }
    }

    // 总分
    std::vector<std::optional<int>> total_scores(data.nrow());
    for (size_t i = 0; i < data.nrow(); ++i) {
        int sum = 0;
        bool has_any = false;
        for (size_t m = 0; m < metric_cols.size(); ++m) {
            if (score_values[m][i]) {
                sum += *score_values[m][i];
                has_any = true;
            }
        }
        if (has_any) total_scores[i] = sum;
    }

    // 输出列 = 原始列 + 新列
    std::vector<std::string> out_headers = data.headers;
    for (const auto& col : metric_cols) {
        out_headers.push_back(col + "_industry_pct");
    }
    for (const auto& col : metric_cols) {
        out_headers.push_back(col + "_industry_score");
    }
    out_headers.push_back("industry_score");

    auto header_idx = [&](const std::string& name) -> size_t {
        for (size_t i = 0; i < out_headers.size(); ++i) {
            if (out_headers[i] == name) return i;
        }
        return out_headers.size();
    };

    // 构建输出表
    std::vector<std::vector<std::string>> out_rows;
    out_rows.reserve(data.nrow());
    for (size_t i = 0; i < data.nrow(); ++i) {
        std::vector<std::string> row(out_headers.size(), "");
        for (size_t j = 0; j < data.headers.size(); ++j) {
            row[j] = data.rows[i][j];
        }
        for (size_t m = 0; m < metric_cols.size(); ++m) {
            row[data.headers.size() + m] = pct_values[m][i] ? format_double(*pct_values[m][i], 4) : "";
            row[data.headers.size() + metric_cols.size() + m] =
                score_values[m][i] ? std::to_string(*score_values[m][i]) : "";
        }
        row.back() = total_scores[i] ? std::to_string(*total_scores[i]) : "";
        out_rows.push_back(std::move(row));
    }

    // 筛选总分 >= SCORE_MIN
    std::vector<std::vector<std::string>> selected;
    std::vector<int> selected_scores;
    for (size_t i = 0; i < out_rows.size(); ++i) {
        auto ts = total_scores[i];
        if (ts && *ts >= SCORE_MIN) {
            selected.push_back(out_rows[i]);
            selected_scores.push_back(*ts);
        }
    }

    // 按 industry_score 降序
    std::vector<size_t> order(selected.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return selected_scores[a] > selected_scores[b];
    });
    std::vector<std::vector<std::string>> sorted_selected;
    sorted_selected.reserve(selected.size());
    for (size_t i : order) sorted_selected.push_back(std::move(selected[i]));

    fs::create_directories(fs::path(output_file).parent_path());
    quant::csv::write_csv(output_file, out_headers, sorted_selected);
    fmt::print("\n筛选完成：共 {} 只股票达标\n", sorted_selected.size());
    fmt::print("已保存：{}\n", output_file);

    // 行业分布统计
    if (industry_opt) {
        struct Stat { double sum = 0.0; std::vector<double> vals; size_t count = 0; };
        std::unordered_map<std::string, std::unordered_map<std::string, Stat>> stats;
        for (size_t i = 0; i < data.nrow(); ++i) {
            std::string ind = data.rows[i][*industry_opt];
            if (ind.empty()) continue;
            for (const auto& col : metric_cols) {
                auto v = get_val(i, col);
                if (!v) continue;
                auto& s = stats[ind][col];
                s.sum += *v;
                s.vals.push_back(*v);
                ++s.count;
            }
        }

        if (!stats.empty()) {
            fmt::print("\n{:=<60}\n", "");
            fmt::print("各行业指标分布（全市场）：\n");
            fmt::print("{:=<60}\n", "");
            for (const auto& [ind, mstat] : stats) {
                fmt::print("行业: {}\n", ind);
                for (const auto& col : metric_cols) {
                    auto it = mstat.find(col);
                    if (it == mstat.end() || it->second.count == 0) continue;
                    const auto& s = it->second;
                    double mean = s.sum / s.count;
                    double med = median_sorted(s.vals);
                    fmt::print("  {:<20s} mean={:>10.2f} median={:>10.2f} count={}\n",
                               col, mean, med, s.count);
                }
            }
        }
    }

    // 可视化
    if (ENABLE_VISUALIZATION && industry_opt) {
        fmt::print("\n生成行业分布可视化...\n");
        fs::create_directories(viz_dir);

        struct ChartSpec { std::string col; std::string label; bool ascending; };
        std::vector<ChartSpec> specs = {
            {"roe", "ROE (%)", false},
            {"grossprofit_margin", "Gross Profit Margin (%)", false},
            {"debt_to_assets", "Debt to Assets (%)", true},
        };

        for (const auto& spec : specs) {
            auto idx = col_opt(spec.col);
            if (!idx) continue;

            std::unordered_map<std::string, std::vector<double>> groups_vals;
            for (size_t i = 0; i < data.nrow(); ++i) {
                std::string ind = data.rows[i][*industry_opt];
                if (ind.empty()) continue;
                auto v = parse_double(data.rows[i][*idx]);
                if (!v) continue;
                groups_vals[ind].push_back(*v);
            }

            std::vector<std::string> labels;
            std::vector<double> means;
            for (auto& [ind, vals] : groups_vals) {
                if (vals.size() < 3) continue;
                double mean = std::accumulate(vals.begin(), vals.end(), 0.0) / vals.size();
                labels.push_back(ind);
                means.push_back(mean);
            }
            if (labels.empty()) continue;

            std::string path = fmt::format("{}/industry_{}.png", viz_dir, spec.col);
            quant::plot::save_horizontal_bar_chart(path, labels, means,
                                                   fmt::format("Industry Distribution: {}", spec.label),
                                                   spec.label, spec.ascending);
            fmt::print("  已保存行业分布图: {}\n", path);
        }
    }

    // 打印达标股票
    if (!sorted_selected.empty()) {
        fmt::print("\n{:=<60}\n", "");
        fmt::print("达标股票（按总分排序）：\n");
        fmt::print("{:=<60}\n", "");
        std::vector<std::string> disp_cols = {
            "stock_code", "stock_name", "industry", "industry_score", "end_date",
            "roe", "netprofit_yoy", "grossprofit_margin", "debt_to_assets", "ocf_to_revenue"
        };
        for (const auto& col : metric_cols) {
            disp_cols.push_back(col + "_industry_score");
        }
        std::vector<size_t> disp_idx;
        for (const auto& c : disp_cols) {
            auto idx = header_idx(c);
            if (idx < out_headers.size()) disp_idx.push_back(idx);
        }
        for (size_t i = 0; i < std::min<size_t>(20, sorted_selected.size()); ++i) {
            for (size_t j = 0; j < disp_idx.size(); ++j) {
                if (j > 0) fmt::print("  ");
                fmt::print("{}", sorted_selected[i][disp_idx[j]]);
            }
            fmt::print("\n");
        }
    }

    return 0;
}
