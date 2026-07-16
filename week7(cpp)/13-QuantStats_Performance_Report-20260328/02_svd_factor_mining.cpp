// 2-SVD因子挖掘与分析
// 对应 week7/13-QuantStats绩效分析与报告-20260328/2-SVD因子挖掘与分析.py
// C++ 中使用自研轻量 SVD 替代 numpy.linalg.svd, 用 RandomForest 替代 XGBoost。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <matplot/matplot.h>

#include "backtest.hpp"
#include "backtest_data_mysql.hpp"
#include "env.hpp"
#include "ml_features.hpp"
#include "ml_preprocessing.hpp"
#include "ml_tree.hpp"
#include "svd.hpp"

using namespace quant;

namespace {

const double NaN = std::numeric_limits<double>::quiet_NaN();

struct StockInfo {
    std::string name;
    std::string industry;
};

const std::map<std::string, StockInfo> STOCK_POOL = {
    {"600519.SH", {"贵州茅台", "食品饮料"}},
    {"000858.SZ", {"五粮液", "食品饮料"}},
    {"002304.SZ", {"洋河股份", "食品饮料"}},
    {"000001.SZ", {"平安银行", "银行"}},
    {"601398.SH", {"工商银行", "银行"}},
    {"600036.SH", {"招商银行", "银行"}},
    {"300750.SZ", {"宁德时代", "新能源"}},
    {"601012.SH", {"隆基绿能", "新能源"}},
    {"002459.SZ", {"晶澳科技", "新能源"}},
    {"688981.SH", {"中芯国际", "科技"}},
    {"002371.SZ", {"北方华创", "科技"}},
    {"688012.SH", {"中微公司", "科技"}},
    {"600276.SH", {"恒瑞医药", "医药"}},
    {"300760.SZ", {"迈瑞医疗", "医药"}},
    {"000538.SZ", {"云南白药", "医药"}},
    {"159941.SZ", {"纳指ETF", "ETF"}},
};

const std::string START_DATE = "2023-01-01";
const std::string END_DATE = "2025-12-31";

// Category English key -> Chinese display name.
const std::map<std::string, std::string> CATEGORY_NAME = {
    {"price_volume", "价量因子"},
    {"momentum", "动量因子"},
    {"volatility", "波动率因子"},
    {"technical", "技术指标因子"},
    {"ma_pattern", "均线与形态因子"},
    {"interaction", "交互因子"},
};

std::map<std::string, double> build_close_map(const std::vector<bt::Bar>& bars) {
    std::map<std::string, double> out;
    for (const auto& b : bars) out[b.date] = b.close;
    return out;
}

std::vector<std::string> common_dates(const std::map<std::string, std::map<std::string, double>>& close_maps) {
    std::set<std::string> common;
    bool first = true;
    for (const auto& kv : close_maps) {
        std::set<std::string> dates;
        for (const auto& d : kv.second) dates.insert(d.first);
        if (first) {
            common = dates;
            first = false;
        } else {
            std::set<std::string> tmp;
            std::set_intersection(common.begin(), common.end(), dates.begin(), dates.end(),
                                  std::inserter(tmp, tmp.begin()));
            common = std::move(tmp);
        }
    }
    return std::vector<std::string>(common.begin(), common.end());
}

bool load_stock_pool(const quant::mysql::Config& cfg,
                     std::map<std::string, std::map<std::string, double>>& close_maps,
                     std::map<std::string, StockInfo>& loaded_info) {
    for (const auto& kv : STOCK_POOL) {
        auto bars = bt::data::load_from_mysql(cfg, kv.first, START_DATE, END_DATE);
        if (static_cast<int>(bars.size()) < 200) {
            fmt::print("  [跳过] {} {}: 数据不足 ({}条)\n", kv.first, kv.second.name, bars.size());
            continue;
        }
        close_maps[kv.first] = build_close_map(bars);
        loaded_info[kv.first] = kv.second;
        fmt::print("  {} {:6s} ({:4s}): {} 个交易日\n",
                   kv.first, kv.second.name, kv.second.industry, bars.size());
    }
    return !close_maps.empty();
}

std::vector<std::vector<double>> build_return_matrix(
    const std::map<std::string, std::map<std::string, double>>& close_maps,
    const std::vector<std::string>& dates,
    std::vector<std::string>& stocks) {
    stocks.clear();
    for (const auto& kv : close_maps) stocks.push_back(kv.first);
    size_t N = stocks.size();
    size_t T = dates.size();
    std::vector<std::vector<double>> R(T, std::vector<double>(N, NaN));
    for (size_t t = 0; t < T; ++t) {
        for (size_t i = 0; i < N; ++i) {
            auto it = close_maps.at(stocks[i]).find(dates[t]);
            if (it != close_maps.at(stocks[i]).end()) R[t][i] = it->second;
        }
    }
    // Compute daily returns, drop rows with any NaN.
    std::vector<std::vector<double>> ret;
    for (size_t t = 1; t < T; ++t) {
        std::vector<double> row(N, NaN);
        bool ok = true;
        for (size_t i = 0; i < N; ++i) {
            double prev = R[t - 1][i];
            double cur = R[t][i];
            if (prev <= 0.0 || std::isnan(prev) || std::isnan(cur)) { ok = false; break; }
            row[i] = cur / prev - 1.0;
        }
        if (ok) ret.push_back(std::move(row));
    }
    return ret;
}

std::vector<double> column_means(const std::vector<std::vector<double>>& R) {
    size_t N = R.empty() ? 0 : R[0].size();
    std::vector<double> means(N, 0.0);
    if (R.empty()) return means;
    for (size_t i = 0; i < N; ++i) {
        double s = 0.0;
        for (const auto& row : R) s += row[i];
        means[i] = s / static_cast<double>(R.size());
    }
    return means;
}

std::vector<std::vector<double>> center_rows(const std::vector<std::vector<double>>& R,
                                             const std::vector<double>& means) {
    auto out = R;
    for (auto& row : out) {
        for (size_t i = 0; i < row.size(); ++i) row[i] -= means[i];
    }
    return out;
}

std::vector<std::vector<double>> transpose(const std::vector<std::vector<double>>& X) {
    if (X.empty()) return {};
    size_t m = X.size();
    size_t n = X[0].size();
    std::vector<std::vector<double>> Y(n, std::vector<double>(m, 0.0));
    for (size_t i = 0; i < m; ++i) {
        for (size_t j = 0; j < n; ++j) Y[j][i] = X[i][j];
    }
    return Y;
}

double vec_mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

double vec_std(const std::vector<double>& v, double mean) {
    if (v.size() < 2) return 0.0;
    double s = 0.0;
    for (double x : v) { double d = x - mean; s += d * d; }
    return std::sqrt(s / static_cast<double>(v.size()));
}

double pearson(const std::vector<double>& a, const std::vector<double>& b) {
    size_t n = a.size();
    if (n == 0 || n != b.size()) return NaN;
    double ma = vec_mean(a), mb = vec_mean(b);
    double num = 0.0, da = 0.0, db = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double xa = a[i] - ma, xb = b[i] - mb;
        num += xa * xb;
        da += xa * xa;
        db += xb * xb;
    }
    double den = std::sqrt(da * db);
    return den > 0.0 ? num / den : NaN;
}

std::string quarter_of(const std::string& date) {
    if (date.size() < 7) return "";
    int year = std::stoi(date.substr(0, 4));
    int month = std::stoi(date.substr(5, 2));
    int q = (month - 1) / 3 + 1;
    return fmt::format("{}Q{}", year, q);
}

std::vector<double> rolling_std(const std::vector<double>& v, size_t window) {
    std::vector<double> out(v.size(), NaN);
    if (window == 0 || v.size() < window) return out;
    for (size_t i = window - 1; i < v.size(); ++i) {
        double m = 0.0;
        for (size_t j = i + 1 - window; j <= i; ++j) m += v[j];
        m /= static_cast<double>(window);
        double s = 0.0;
        for (size_t j = i + 1 - window; j <= i; ++j) {
            double d = v[j] - m; s += d * d;
        }
        out[i] = std::sqrt(s / static_cast<double>(window));
    }
    return out;
}

std::vector<double> cumulative_sum(const std::vector<double>& v) {
    std::vector<double> out(v.size(), 0.0);
    double s = 0.0;
    for (size_t i = 0; i < v.size(); ++i) { s += v[i]; out[i] = s; }
    return out;
}

void save_svd_scree_plot(const std::vector<double>& explained,
                         const std::vector<double>& cumulative) {
    namespace mp = matplot;
    auto fig = mp::figure(false);
    fig->size(1400, 600);
    size_t n = explained.size();
    std::vector<double> xs(n);
    for (size_t i = 0; i < n; ++i) xs[i] = static_cast<double>(i + 1);

    auto ax1 = mp::subplot(1, 2, 0);
    ax1->bar(xs, explained)->face_color("steelblue");
    ax1->xlabel("Principal Component");
    ax1->ylabel("Variance Explained");
    ax1->title("SVD Scree Plot");
    ax1->grid(mp::on);

    auto ax2 = mp::subplot(1, 2, 1);
    ax2->plot(xs, cumulative, "-o")->line_width(2);
    ax2->plot({xs.front(), xs.back()}, {0.8, 0.8}, "r--")->line_width(1.5);
    ax2->plot({xs.front(), xs.back()}, {0.9, 0.9}, "g--")->line_width(1.5);
    ax2->xlabel("Number of Components");
    ax2->ylabel("Cumulative Variance");
    ax2->title("Cumulative Variance Explained");
    ax2->grid(mp::on);

    fig->save("outputs/week7/svd_scree_plot.png");
    fmt::print("  图表已保存: outputs/week7/svd_scree_plot.png\n");
}

void save_factor_timeseries_plot(const std::vector<std::string>& dates,
                                 const std::vector<std::vector<double>>& factor_returns) {
    namespace mp = matplot;
    size_t n_show = factor_returns.size();
    if (n_show == 0) return;
    auto fig = mp::figure(false);
    fig->size(1400, static_cast<int>(n_show) * 300);
    std::vector<double> xs(dates.size());
    for (size_t i = 0; i < dates.size(); ++i) xs[i] = static_cast<double>(i);
    for (size_t k = 0; k < n_show; ++k) {
        auto ax = mp::subplot(static_cast<int>(n_show), 1, static_cast<int>(k));
        auto cum = cumulative_sum(factor_returns[k]);
        ax->plot(xs, cum)->line_width(1.5);
        ax->ylabel(fmt::format("Factor {} CumRet", k + 1));
        ax->title(fmt::format("Hidden Factor #{} Cumulative Return", k + 1));
        ax->grid(mp::on);
    }
    mp::xlabel("Time");
    fig->save("outputs/week7/svd_factor_timeseries.png");
    fmt::print("  图表已保存: outputs/week7/svd_factor_timeseries.png\n");
}

void save_rolling_concentration_plot(const std::vector<std::string>& /*mid_dates*/,
                                     const std::vector<double>& top1,
                                     const std::vector<double>& top3) {
    namespace mp = matplot;
    auto fig = mp::figure(false);
    fig->size(1200, 600);
    std::vector<double> xs(top1.size());
    for (size_t i = 0; i < top1.size(); ++i) xs[i] = static_cast<double>(i);
    auto ax = fig->current_axes();
    ax->plot(xs, top1, "-")->line_width(2).display_name("Factor 1");
    ax->plot(xs, top3, "-")->line_width(2).display_name("Top 3");
    ax->plot({xs.front(), xs.back()}, {0.5, 0.5}, "r--")->line_width(1.5).display_name("50%");
    ax->xlabel("Window");
    ax->ylabel("Variance Explained");
    ax->title("Rolling SVD: Factor Concentration Over Time");
    ax->legend();
    ax->grid(mp::on);
    fig->save("outputs/week7/svd_rolling_concentration.png");
    fmt::print("  图表已保存: outputs/week7/svd_rolling_concentration.png\n");
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    auto env = quant::env::find_and_load_dotenv();
    auto cfg = bt::data::load_mysql_config(env);

    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("  SVD因子挖掘与分析\n");
    fmt::print("{0}\n", std::string(80, '='));

    // Ensure output dir.
    std::filesystem::create_directories("outputs/week7");

    // Step 1: load data and build return matrix.
    fmt::print("\n第1步: 构建收益率矩阵 R (N x T)\n");
    std::map<std::string, std::map<std::string, double>> close_maps;
    std::map<std::string, StockInfo> stock_info;
    if (!load_stock_pool(cfg, close_maps, stock_info)) {
        fmt::print("  没有加载到数据, 程序退出\n");
        return 1;
    }
    auto dates = common_dates(close_maps);
    std::vector<std::string> stocks;
    auto returns = build_return_matrix(close_maps, dates, stocks);
    std::vector<std::string> ret_dates(dates.begin() + 1, dates.end());
    size_t N = stocks.size();
    size_t T = returns.size();
    fmt::print("\n  收益率矩阵 R: {} 只股票 x {} 个交易日\n", N, T);
    fmt::print("  时间范围: {} ~ {}\n", dates.front(), dates.back());

    // Step 2: SVD.
    fmt::print("\n第2步: SVD分解 + 奇异值衰减分析\n");
    auto means = column_means(returns);
    auto R_centered = center_rows(returns, means);
    auto RcT = transpose(R_centered);  // N x T
    std::vector<std::vector<double>> U, Vt;
    std::vector<double> sigma;
    if (!quant::svd(RcT, U, sigma, Vt)) {
        fmt::print("  SVD 分解失败\n");
        return 1;
    }

    double total_var = 0.0;
    for (double s : sigma) total_var += s * s;
    std::vector<double> explained(sigma.size()), cumulative(sigma.size());
    for (size_t i = 0; i < sigma.size(); ++i) {
        explained[i] = sigma[i] * sigma[i] / total_var;
        cumulative[i] = (i == 0) ? explained[i] : cumulative[i - 1] + explained[i];
    }

    fmt::print("\n  SVD分解完成: R_centered ({} x {})\n", RcT.size(), RcT[0].size());
    fmt::print("  奇异值个数: {}\n", sigma.size());
    fmt::print("\n  奇异值衰减分析:\n");
    fmt::print("    {:>6}  {:>10}  {:>10}  {:>10}\n", "因子#", "奇异值", "方差占比", "累积占比");
    fmt::print("    {0}  {0}  {0}  {0}\n", std::string(10, '-'));
    size_t show_n = std::min<size_t>(10, sigma.size());
    for (size_t i = 0; i < show_n; ++i) {
        fmt::print("    {:>6}  {:>10.4f}  {:>10.2f}%  {:>10.2f}%\n",
                   i + 1, sigma[i], explained[i] * 100.0, cumulative[i] * 100.0);
    }

    size_t n_factors_80 = 0, n_factors_90 = 0;
    for (size_t i = 0; i < cumulative.size(); ++i) {
        if (n_factors_80 == 0 && cumulative[i] >= 0.80) n_factors_80 = i + 1;
        if (n_factors_90 == 0 && cumulative[i] >= 0.90) n_factors_90 = i + 1;
    }
    fmt::print("\n  结论:\n");
    fmt::print("    累积方差达80%需要 {} 个因子\n", n_factors_80);
    fmt::print("    累积方差达90%需要 {} 个因子\n", n_factors_90);
    if (cumulative.size() >= 3) {
        fmt::print("    前3个因子解释 {:.1f}% 的总方差\n", cumulative[2] * 100.0);
    }
    save_svd_scree_plot(explained, cumulative);

    // Step 3: factor interpretation.
    fmt::print("\n第3步: 隐因子解读 -- 股票在隐因子上的暴露\n");
    size_t n_show_factors = std::min<size_t>(5, U[0].size());
    for (size_t k = 0; k < n_show_factors; ++k) {
        fmt::print("\n  --- 隐因子 #{} (方差占比: {:.1f}%) ---\n",
                   k + 1, explained[k] * 100.0);
        std::vector<size_t> idx(N);
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(),
                  [&U, k](size_t a, size_t b) { return U[a][k] < U[b][k]; });

        fmt::print("    正暴露 top3:\n");
        for (size_t r = 0; r < 3; ++r) {
            size_t i = idx[N - 1 - r];
            const auto& info = stock_info.at(stocks[i]);
            fmt::print("      {} {:6s} ({:4s})  暴露={:+.4f}\n",
                       stocks[i], info.name, info.industry, U[i][k]);
        }
        fmt::print("    负暴露 top3:\n");
        for (size_t r = 0; r < 3; ++r) {
            size_t i = idx[r];
            const auto& info = stock_info.at(stocks[i]);
            fmt::print("      {} {:6s} ({:4s})  暴露={:+.4f}\n",
                       stocks[i], info.name, info.industry, U[i][k]);
        }
        std::map<std::string, std::vector<double>> ind_exp;
        for (size_t i = 0; i < N; ++i) {
            const auto& ind = stock_info.at(stocks[i]).industry;
            ind_exp[ind].push_back(U[i][k]);
        }
        fmt::print("    行业平均暴露:\n");
        for (const auto& kv : ind_exp) {
            double m = vec_mean(kv.second);
            fmt::print("      {:6s}: {:+.4f}\n", kv.first, m);
        }
    }

    // Step 4: factor time series.
    fmt::print("\n第4步: 隐因子时间序列 -- 不同时段的驱动力变化\n");
    size_t n_show_ts = std::min<size_t>(3, Vt.size());
    std::vector<std::vector<double>> factor_returns(n_show_ts);
    for (size_t k = 0; k < n_show_ts; ++k) {
        factor_returns[k].resize(Vt[k].size());
        for (size_t t = 0; t < Vt[k].size(); ++t) {
            factor_returns[k][t] = Vt[k][t] * sigma[k];
        }
    }

    fmt::print("\n  各隐因子季度平均收益:\n");
    fmt::print("    {:<10}", "季度");
    for (size_t k = 0; k < n_show_ts; ++k) fmt::print("  {:>8}", fmt::format("因子{}", k + 1));
    fmt::print("\n");
    std::set<std::string> quarters;
    for (const auto& d : ret_dates) quarters.insert(quarter_of(d));
    for (const auto& q : quarters) {
        fmt::print("    {:<10}", q);
        for (size_t k = 0; k < n_show_ts; ++k) {
            std::vector<double> vals;
            for (size_t t = 0; t < ret_dates.size(); ++t) {
                if (quarter_of(ret_dates[t]) == q) vals.push_back(factor_returns[k][t]);
            }
            double m = vec_mean(vals);
            fmt::print("  {:>+7.3f}%", m * 100.0);
        }
        fmt::print("\n");
    }

    size_t window = 60;
    fmt::print("\n  因子活跃度 ({}日滚动标准差):\n", window);
    for (size_t k = 0; k < n_show_ts; ++k) {
        auto rstd = rolling_std(factor_returns[k], window);
        double peak_val = -1.0, trough_val = 1e300;
        size_t peak_idx = 0, trough_idx = 0;
        for (size_t i = window - 1; i < rstd.size(); ++i) {
            if (rstd[i] > peak_val) { peak_val = rstd[i]; peak_idx = i; }
            if (rstd[i] < trough_val) { trough_val = rstd[i]; trough_idx = i; }
        }
        fmt::print("    因子{}: 最活跃={}({:.4f}), 最沉寂={}({:.4f})\n",
                   k + 1, ret_dates[peak_idx], peak_val, ret_dates[trough_idx], trough_val);
    }
    save_factor_timeseries_plot(ret_dates, factor_returns);

    // Step 5: industry factor structure.
    fmt::print("\n第5步: 行业因子结构对比\n");
    size_t n_factors = std::min<size_t>(5, U[0].size());
    std::map<std::string, std::vector<size_t>> industry_groups;
    for (size_t i = 0; i < N; ++i) {
        industry_groups[stock_info.at(stocks[i]).industry].push_back(i);
    }
    fmt::print("\n  行业因子暴露 (前{}个隐因子):\n", n_factors);
    fmt::print("    {:8s}", "行业");
    for (size_t k = 0; k < n_factors; ++k) fmt::print("  {:>8}", fmt::format("F{}", k + 1));
    fmt::print("\n");
    std::map<std::string, std::vector<double>> industry_profiles;
    for (const auto& kv : industry_groups) {
        std::vector<double> profile(n_factors, 0.0);
        for (size_t k = 0; k < n_factors; ++k) {
            double s = 0.0;
            for (size_t idx : kv.second) s += U[idx][k];
            profile[k] = s / kv.second.size();
        }
        industry_profiles[kv.first] = profile;
        fmt::print("    {:8s}", kv.first);
        for (size_t k = 0; k < n_factors; ++k) fmt::print("  {:>+8.4f}", profile[k]);
        fmt::print("\n");
    }
    auto ind_names = industry_profiles;
    fmt::print("\n  行业间距离矩阵 (欧氏距离):\n");
    fmt::print("    {:8s}", "");
    for (const auto& kv : industry_profiles) fmt::print("  {:>8s}", kv.first);
    fmt::print("\n");
    double max_dist = -1.0, min_dist = 1e300;
    std::string max_i, max_j, min_i, min_j;
    std::vector<std::string> inames;
    for (const auto& kv : industry_profiles) inames.push_back(kv.first);
    for (size_t i = 0; i < inames.size(); ++i) {
        fmt::print("    {:8s}", inames[i]);
        for (size_t j = 0; j < inames.size(); ++j) {
            if (i == j) {
                fmt::print("  {:>8s}", "---");
                continue;
            }
            double d = 0.0;
            for (size_t k = 0; k < n_factors; ++k) {
                double diff = industry_profiles[inames[i]][k] - industry_profiles[inames[j]][k];
                d += diff * diff;
            }
            d = std::sqrt(d);
            fmt::print("  {:>8.4f}", d);
            if (d > max_dist) { max_dist = d; max_i = inames[i]; max_j = inames[j]; }
            if (d < min_dist) { min_dist = d; min_i = inames[i]; min_j = inames[j]; }
        }
        fmt::print("\n");
    }
    fmt::print("\n  最相似行业对: {} - {} (距离={:.4f})\n", min_i, min_j, min_dist);
    fmt::print("  最不同行业对: {} - {} (距离={:.4f})\n", max_i, max_j, max_dist);

    // Step 6: rolling SVD.
    fmt::print("\n第6步: 滚动窗口SVD -- 市场驱动因子的时变性\n");
    size_t roll_window = 120;
    size_t roll_step = 20;
    std::vector<std::string> roll_dates;
    std::vector<double> roll_top1, roll_top3;
    for (size_t start = 0; start + roll_window <= returns.size(); start += roll_step) {
        std::vector<std::vector<double>> window_data(returns.begin() + start,
                                                     returns.begin() + start + roll_window);
        auto wmeans = column_means(window_data);
        auto wcentered = center_rows(window_data, wmeans);
        auto wT = transpose(wcentered);
        std::vector<std::vector<double>> wU, wVt;
        std::vector<double> wSigma;
        if (!quant::svd(wT, wU, wSigma, wVt)) continue;
        double tvar = 0.0;
        for (double s : wSigma) tvar += s * s;
        double top1 = (tvar > 0.0) ? (wSigma[0] * wSigma[0] / tvar) : 0.0;
        double top3 = 0.0;
        for (size_t i = 0; i < std::min<size_t>(3, wSigma.size()); ++i) {
            top3 += wSigma[i] * wSigma[i] / tvar;
        }
        roll_dates.push_back(ret_dates[start + roll_window / 2]);
        roll_top1.push_back(top1);
        roll_top3.push_back(top3);
    }
    fmt::print("\n  滚动窗口: {}天, 步长: {}天, 共 {} 个窗口\n", roll_window, roll_step, roll_dates.size());
    fmt::print("\n  第一因子方差占比:\n");
    fmt::print("    {:<12}  {:>12}  {:>10}  {:>12}\n", "时间段", "Factor1占比", "Top3占比", "市场状态");
    fmt::print("    {0}  {0}  {0}  {0}\n", std::string(12, '-'));
    for (size_t i = 0; i < roll_dates.size(); ++i) {
        std::string month = roll_dates[i].substr(0, 7);
        std::string state;
        if (roll_top1[i] > 0.50) state = "齐涨齐跌";
        else if (roll_top1[i] > 0.35) state = "板块分化";
        else state = "个股行情";
        fmt::print("    {:<12}  {:>11.1f}%  {:>9.1f}%  {:>12}\n",
                   month, roll_top1[i] * 100.0, roll_top3[i] * 100.0, state);
    }
    double mean_f1 = vec_mean(roll_top1);
    auto [min_it, max_it] = std::minmax_element(roll_top1.begin(), roll_top1.end());
    fmt::print("\n  统计:\n");
    fmt::print("    Factor1平均占比: {:.1f}%\n", mean_f1 * 100.0);
    fmt::print("    Factor1最高: {:.1f}% ({})\n", *max_it * 100.0, roll_dates[max_it - roll_top1.begin()]);
    fmt::print("    Factor1最低: {:.1f}% ({})\n", *min_it * 100.0, roll_dates[min_it - roll_top1.begin()]);
    save_rolling_concentration_plot(roll_dates, roll_top1, roll_top3);

    // Step 7: feature compression + prediction comparison.
    fmt::print("\n第7步: SVD因子压缩 -- 50+因子 vs 主成分降维\n");
    std::string test_stock = "600519.SH";
    auto test_bars = bt::data::load_from_mysql(cfg, test_stock, START_DATE, END_DATE);
    if (!test_bars.empty()) {
        auto features = ml::calc_features(test_bars);
        auto all_cols = ml::all_feature_names();
        std::vector<std::string> feature_cols;
        for (const auto& c : all_cols) {
            if (features.find(c) != features.end()) feature_cols.push_back(c);
        }
        // Build X, y; drop rows with NaN.
        std::vector<std::vector<double>> X;
        std::vector<int> y;
        for (size_t i = 0; i + 1 < test_bars.size(); ++i) {
            std::vector<double> row;
            bool ok = true;
            for (const auto& c : feature_cols) {
                auto it = features.find(c);
                if (it == features.end() || std::isnan(it->second[i])) { ok = false; break; }
                row.push_back(it->second[i]);
            }
            if (!ok) continue;
            int label = test_bars[i + 1].close > test_bars[i].close ? 1 : 0;
            X.push_back(std::move(row));
            y.push_back(label);
        }
        if (!X.empty()) {
            // Z-score.
            size_t D = X[0].size();
            std::vector<double> Xmean(D, 0.0), Xstd(D, 0.0);
            for (const auto& row : X) for (size_t d = 0; d < D; ++d) Xmean[d] += row[d];
            for (size_t d = 0; d < D; ++d) Xmean[d] /= X.size();
            for (const auto& row : X) for (size_t d = 0; d < D; ++d) {
                double diff = row[d] - Xmean[d]; Xstd[d] += diff * diff;
            }
            for (size_t d = 0; d < D; ++d) {
                Xstd[d] = std::sqrt(Xstd[d] / X.size());
                if (Xstd[d] == 0.0) Xstd[d] = 1.0;
            }
            auto Xnorm = X;
            for (auto& row : Xnorm) for (size_t d = 0; d < D; ++d) row[d] = (row[d] - Xmean[d]) / Xstd[d];

            std::vector<std::vector<double>> Uf, Vtf;
            std::vector<double> Sf;
            quant::svd(Xnorm, Uf, Sf, Vtf);
            double total_var_f = 0.0;
            for (double s : Sf) total_var_f += s * s;
            std::vector<double> cum_var_f(Sf.size());
            for (size_t i = 0; i < Sf.size(); ++i) {
                cum_var_f[i] = (i == 0) ? (Sf[i] * Sf[i] / total_var_f)
                                        : cum_var_f[i - 1] + Sf[i] * Sf[i] / total_var_f;
            }

            fmt::print("\n  股票: {} ({})\n", test_stock, stock_info.at(test_stock).name);
            fmt::print("  原始因子数: {}\n", feature_cols.size());
            fmt::print("  样本量: {}\n", X.size());
            fmt::print("\n  不同主成分数量的方差保留率:\n");
            std::vector<size_t> k_values = {3, 5, 8, 10, 15, feature_cols.size()};
            for (size_t k : k_values) {
                size_t kr = std::min(k, Sf.size());
                double vp = (kr == 0) ? 0.0 : cum_var_f[kr - 1];
                fmt::print("    K={:>3}: 保留 {:.1f}% 的方差\n", kr, vp * 100.0);
            }

            size_t train_size = static_cast<size_t>(X.size() * 0.7);
            auto Xtrain_raw = std::vector<std::vector<double>>(Xnorm.begin(), Xnorm.begin() + train_size);
            auto Xtest_raw = std::vector<std::vector<double>>(Xnorm.begin() + train_size, Xnorm.end());
            auto ytrain = std::vector<int>(y.begin(), y.begin() + train_size);
            auto ytest = std::vector<int>(y.begin() + train_size, y.end());

            struct Result { std::string name; size_t dim; double var_pct; double auc; double acc; };
            std::vector<Result> results;

            auto eval_model = [](const std::vector<std::vector<double>>& Xtr,
                                 const std::vector<int>& ytr,
                                 const std::vector<std::vector<double>>& Xte,
                                 const std::vector<int>& yte) {
                ml::RandomForestClassifier model(50, 4, 10);
                std::vector<double> ytr_d(ytr.begin(), ytr.end());
                model.fit(Xtr, ytr_d);
                std::vector<int> ypred;
                std::vector<double> yprob;
                for (const auto& x : Xte) {
                    double prob = model.predict_proba(x);
                    yprob.push_back(prob);
                    ypred.push_back(prob > 0.5 ? 1 : 0);
                }
                auto m = ml::evaluate_classification(yte, ypred, yprob);
                return std::make_pair(m.auc, m.accuracy);
            };

            auto [auc_full, acc_full] = eval_model(Xtrain_raw, ytrain, Xtest_raw, ytest);
            results.push_back({"全部因子", feature_cols.size(), 100.0, auc_full, acc_full});

            for (size_t k : {3, 5, 8, 10, 15}) {
                if (k >= Sf.size()) continue;
                std::vector<std::vector<double>> Xsvd(Xnorm.size(), std::vector<double>(k, 0.0));
                for (size_t i = 0; i < Xnorm.size(); ++i) {
                    for (size_t j = 0; j < k; ++j) Xsvd[i][j] = Uf[i][j] * Sf[j];
                }
                auto Xtr_k = std::vector<std::vector<double>>(Xsvd.begin(), Xsvd.begin() + train_size);
                auto Xte_k = std::vector<std::vector<double>>(Xsvd.begin() + train_size, Xsvd.end());
                auto [auc_k, acc_k] = eval_model(Xtr_k, ytrain, Xte_k, ytest);
                double vp = cum_var_f[k - 1] * 100.0;
                results.push_back({fmt::format("SVD K={}", k), k, vp, auc_k, acc_k});
            }

            fmt::print("\n  RandomForest预测效果对比:\n");
            fmt::print("    {:12s}  {:>6}  {:>8}  {:>8}  {:>8}\n",
                       "方法", "维度", "方差保留", "AUC", "Accuracy");
            fmt::print("    {0}  {0}  {0}  {0}  {0}\n",
                       std::string(12, '-'));
            Result best_svd = results.front();
            for (const auto& r : results) {
                fmt::print("    {:12s}  {:>6}  {:>7.1f}%  {:>8.4f}  {:>8.4f}\n",
                           r.name, r.dim, r.var_pct, r.auc, r.acc);
                if (r.name != "全部因子" && r.auc > best_svd.auc) best_svd = r;
            }
            fmt::print("\n  结论: 全量因子 AUC={:.4f}, {} AUC={:.4f}\n",
                       auc_full, best_svd.name, best_svd.auc);

            // Step 7.5: factor tracing.
            fmt::print("\n第7.5步: 隐因子溯源 -- SVD因子到底是什么?\n");
            auto taxonomy = ml::feature_taxonomy();
            std::map<std::string, std::string> feat_to_cat;
            for (const auto& kv : taxonomy) {
                for (const auto& f : kv.second) feat_to_cat[f] = kv.first;
            }
            size_t n_show_trace = std::min<size_t>(5, Vtf.size());
            for (size_t k = 0; k < n_show_trace; ++k) {
                double vp = (Sf[k] * Sf[k]) / total_var_f;
                double cp = cum_var_f[k];
                fmt::print("  --- PC{} (方差占比: {:.1f}%, 累积: {:.1f}%) ---\n", k + 1, vp * 100.0, cp * 100.0);
                const auto& loadings = Vtf[k];
                std::vector<size_t> idx(loadings.size());
                std::iota(idx.begin(), idx.end(), 0);
                std::sort(idx.begin(), idx.end(),
                          [&loadings](size_t a, size_t b) {
                              return std::abs(loadings[a]) > std::abs(loadings[b]);
                          });
                fmt::print("    原始因子权重 top8:\n");
                fmt::print("      {:<25s} {:>8} {:>10}\n", "原始因子", "权重", "分类");
                std::map<std::string, double> cat_weights;
                for (size_t i = 0; i < loadings.size(); ++i) {
                    const auto& feat = feature_cols[i];
                    auto it = feat_to_cat.find(feat);
                    std::string cat = (it == feat_to_cat.end()) ? "其他" : CATEGORY_NAME.at(it->second);
                    cat_weights[cat] += std::abs(loadings[i]);
                }
                for (size_t r = 0; r < std::min<size_t>(8, idx.size()); ++r) {
                    size_t i = idx[r];
                    const auto& feat = feature_cols[i];
                    auto it = feat_to_cat.find(feat);
                    std::string cat = (it == feat_to_cat.end()) ? "其他" : CATEGORY_NAME.at(it->second);
                    char sign = loadings[i] > 0 ? '+' : '-';
                    fmt::print("      {:<25s} {}{:>7.4f} {:>10}\n",
                               feat, sign, std::abs(loadings[i]), cat);
                }
                fmt::print("\n    按因子类别汇总权重:\n");
                std::vector<std::pair<std::string, double>> cw(cat_weights.begin(), cat_weights.end());
                std::sort(cw.begin(), cw.end(),
                          [](const auto& a, const auto& b) { return a.second > b.second; });
                for (const auto& kv : cw) {
                    int bar_len = static_cast<int>(kv.second * 30);
                    std::string bar(bar_len, '#');
                    fmt::print("      {:12s}: {:.3f}  {}\n", kv.first, kv.second, bar);
                }
                if (!cw.empty()) {
                    std::string top = cw[0].first;
                    std::string second = (cw.size() > 1) ? cw[1].first : "";
                    fmt::print("\n    PC{} 主导类别: {}, {}\n", k + 1, top, second);
                }
            }
        }
    }

    // Step 8: residual analysis.
    fmt::print("\n第8步: 残差分析 -- 发现走独立行情的个股\n");
    std::vector<double> market_ret(T, 0.0);
    for (size_t t = 0; t < T; ++t) {
        for (size_t i = 0; i < N; ++i) market_ret[t] += returns[t][i];
        market_ret[t] /= static_cast<double>(N);
    }
    std::map<std::string, std::vector<double>> industry_returns;
    for (size_t i = 0; i < N; ++i) {
        const auto& ind = stock_info.at(stocks[i]).industry;
        if (industry_returns.find(ind) == industry_returns.end()) {
            industry_returns[ind].assign(T, 0.0);
            industry_returns[ind + "_cnt"].assign(T, 0.0);
        }
    }
    // Simpler: accumulate.
    std::map<std::string, std::vector<double>> ind_sum, ind_cnt;
    for (size_t i = 0; i < N; ++i) {
        const auto& ind = stock_info.at(stocks[i]).industry;
        if (ind_sum.find(ind) == ind_sum.end()) {
            ind_sum[ind].assign(T, 0.0);
            ind_cnt[ind].assign(T, 0.0);
        }
        for (size_t t = 0; t < T; ++t) {
            ind_sum[ind][t] += returns[t][i];
            ind_cnt[ind][t] += 1.0;
        }
    }
    std::map<std::string, std::vector<double>> ind_ret;
    for (const auto& kv : ind_sum) {
        ind_ret[kv.first].resize(T);
        for (size_t t = 0; t < T; ++t) {
            ind_ret[kv.first][t] = (ind_cnt[kv.first][t] > 0.0)
                                       ? kv.second[t] / ind_cnt[kv.first][t]
                                       : 0.0;
        }
    }

    size_t n_show_res = std::min<size_t>(3, Vt.size());
    fmt::print("\n  隐因子 vs 等权大盘/行业 相关性:\n");
    fmt::print("    {:12s}  {:>10}", "", "等权大盘");
    for (const auto& kv : ind_ret) fmt::print("  {:>8}", kv.first);
    fmt::print("\n");
    for (size_t k = 0; k < n_show_res; ++k) {
        std::vector<double> fts(Vt[k].size());
        for (size_t t = 0; t < Vt[k].size(); ++t) fts[t] = Vt[k][t] * sigma[k];
        fmt::print("    Factor{:5d}  {:>+10.3f}", k + 1, pearson(fts, market_ret));
        for (const auto& kv : ind_ret) {
            fmt::print("  {:>+8.3f}", pearson(fts, kv.second));
        }
        fmt::print("\n");
    }

    size_t n_reconstruct = std::min<size_t>(5, sigma.size());
    std::vector<std::vector<double>> R_approx(N, std::vector<double>(T, 0.0));
    for (size_t i = 0; i < N; ++i) {
        for (size_t t = 0; t < T; ++t) {
            double sum = 0.0;
            for (size_t k = 0; k < n_reconstruct; ++k) {
                sum += U[i][k] * sigma[k] * Vt[k][t];
            }
            R_approx[i][t] = sum;
        }
    }

    struct ResidualStat {
        std::string code, name, industry;
        double res_std = 0.0, unexplained = 0.0;
    };
    std::vector<ResidualStat> res_stats;
    for (size_t i = 0; i < N; ++i) {
        double res_total = 0.0, total_var = 0.0;
        std::vector<double> resid(T);
        for (size_t t = 0; t < T; ++t) {
            resid[t] = R_centered[t][i] - R_approx[i][t];
            res_total += resid[t] * resid[t];
            total_var += R_centered[t][i] * R_centered[t][i];
        }
        ResidualStat rs;
        rs.code = stocks[i];
        rs.name = stock_info.at(stocks[i]).name;
        rs.industry = stock_info.at(stocks[i]).industry;
        rs.res_std = vec_std(resid, vec_mean(resid));
        rs.unexplained = (total_var > 0.0) ? res_total / total_var : 0.0;
        res_stats.push_back(rs);
    }
    std::sort(res_stats.begin(), res_stats.end(),
              [](const auto& a, const auto& b) { return a.unexplained > b.unexplained; });

    fmt::print("\n  使用前{}个隐因子重构, 各股残差占比:\n", n_reconstruct);
    fmt::print("    {:12s} {:8s} {:6s} {:>8} {:>8} {:>15}\n",
               "代码", "名称", "行业", "残差占比", "残差波动", "标签");
    for (const auto& rs : res_stats) {
        std::string label;
        if (rs.unexplained > 0.5) label = "独立行情强";
        else if (rs.unexplained > 0.3) label = "有alpha信号";
        else label = "跟随大盘";
        fmt::print("    {:12s} {:8s} {:6s} {:>7.1f}% {:>8.4f} {:>15}\n",
                   rs.code, rs.name, rs.industry, rs.unexplained * 100.0, rs.res_std, label);
    }

    fmt::print("\n{0}\n", std::string(80, '='));
    fmt::print("  SVD因子挖掘分析完成\n");
    fmt::print("{0}\n", std::string(80, '='));
    return 0;
}
