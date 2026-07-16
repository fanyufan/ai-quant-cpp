#pragma once

// ============================================================================
// factor_lib.hpp -- 多因子选股因子库 (header-only, 纯算法, 无 DB 依赖)
//
// 转换自 week11/21-投资晨会-20260425/CASE-C-多因子选股/ 的 3 个 Python 模块:
//   factor_lib.py    -- 10 个技术面 + 流动性因子 (公式严格对齐 Python)
//   preprocessor.py  -- MAD 去极值 + Z-score + 行业中性化 "三件套"
//   synthesizer.py   -- 等权 / IC 加权合成 + IC / IR (Lasso 按任务要求不转换)
//
// 10 个因子 (方向统一为"越大越好", 取负号的因子在公式中已处理):
//   动量:   MOM_1M(21 日) MOM_3M(63 日) MOM_6M(126 日) 累计收益率
//   反转:   REV_5D = -(5 日收益率)
//   波动:   VOL_20 / VOL_60 = -(年化波动率, 样本 std * sqrt(250))
//   流动性: LIQ_20  = -log(20 日日均成交额)
//           TURN_20 = -(20 日平均换手率)
//   技术:   RSI_14 = RSI(14) - 50   BIAS_20 = -(close-MA20)/MA20
//
// 与 Python 的口径差异 (有意为之, 不影响结论方向):
//   1. zscore 用总体 std (ddof=0), Python preprocessor.py 用样本 std (ddof=1);
//      差异仅为全体乘以常数 sqrt((N-1)/N), 对秩相关 IC 与等频分层无影响.
//   2. TURN_20 优先使用真实换手率 (KlineRec.turnover_rate, 即"成交量/流通股本x100",
//      与 Python total_share>0 分支同口径); 全部缺失时退回 Python total_share=0
//      分支的 "20 日均量 / 60 日均量" 相对代理.
//   3. RSI_14 用简单移动平均 (Cutler 口径, 与 Python rolling(14).mean() 一致),
//      不复用 quant::ind::rsi (那是 Wilder 平滑口径).
// ============================================================================

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace quant::factor {

inline double quiet_nan() { return std::numeric_limits<double>::quiet_NaN(); }

// 单根日 K 线 (amount / turnover_rate 允许 NaN 表示缺失)
struct KlineRec {
    std::string date;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    double amount = 0.0;
    double turnover_rate = std::numeric_limits<double>::quiet_NaN();
};

// code -> factor_name -> value (对应 Python 的因子 DataFrame, index=股票, columns=因子)
using FactorRows = std::map<std::string, std::map<std::string, double>>;
// code -> 行业名
using IndustryMap = std::unordered_map<std::string, std::string>;

// 10 个因子名 (固定顺序, 与 Python factor_lib.py 输出列一致)
inline const std::vector<std::string>& factor_names() {
    static const std::vector<std::string> kNames = {
        "MOM_1M", "MOM_3M", "MOM_6M", "REV_5D", "VOL_20", "VOL_60",
        "LIQ_20", "TURN_20", "RSI_14", "BIAS_20"};
    return kNames;
}

// ---------------------------------------------------------------------------
// 内部小工具
// ---------------------------------------------------------------------------
namespace detail {

// 中位数 (输入不含 NaN; 偶数个取中间两值平均, 与 pandas 一致)
inline double median(std::vector<double> v) {
    if (v.empty()) return quiet_nan();
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n % 2 == 1) return v[n / 2];
    return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

// 均值 (空 -> NaN)
inline double mean(const std::vector<double>& v) {
    if (v.empty()) return quiet_nan();
    return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

// 跳过 NaN 的均值/标准差; ddof=0 总体 / ddof=1 样本
// 返回有效值个数; mean_out 在 count>=1 时有效, std_out 在 count>ddof 时有效 (否则 NaN)
inline size_t mean_std(const std::vector<double>& vals, int ddof,
                       double& mean_out, double& std_out) {
    std::vector<double> valid;
    valid.reserve(vals.size());
    for (double x : vals) {
        if (!std::isnan(x)) valid.push_back(x);
    }
    mean_out = quiet_nan();
    std_out = quiet_nan();
    if (valid.empty()) return 0;
    double m = mean(valid);
    mean_out = m;
    if (valid.size() > static_cast<size_t>(ddof)) {
        double ss = 0.0;
        for (double x : valid) ss += (x - m) * (x - m);
        std_out = std::sqrt(ss / static_cast<double>(valid.size() - static_cast<size_t>(ddof)));
    }
    return valid.size();
}

// 取尾部 tail_n 个元素并剔除 NaN (对应 pandas s.tail(n) 后 skipna 统计)
inline std::vector<double> tail_valid(const std::vector<double>& v, size_t tail_n) {
    size_t start = v.size() > tail_n ? v.size() - tail_n : 0;
    std::vector<double> out;
    out.reserve(v.size() - start);
    for (size_t i = start; i < v.size(); ++i) {
        if (!std::isnan(v[i])) out.push_back(v[i]);
    }
    return out;
}

// 平均秩 (并列取平均秩, 与 scipy.stats.rankdata 默认行为一致); 输入不含 NaN
inline std::vector<double> average_ranks(const std::vector<double>& v) {
    size_t n = v.size();
    std::vector<size_t> ord(n);
    std::iota(ord.begin(), ord.end(), 0);
    std::stable_sort(ord.begin(), ord.end(),
                     [&](size_t a, size_t b) { return v[a] < v[b]; });
    std::vector<double> ranks(n, 0.0);
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j + 1 < n && v[ord[j + 1]] == v[ord[i]]) ++j;
        double avg = (static_cast<double>(i) + static_cast<double>(j)) / 2.0 + 1.0;  // 1-based
        for (size_t k = i; k <= j; ++k) ranks[ord[k]] = avg;
        i = j + 1;
    }
    return ranks;
}

// Pearson 相关系数 (输入不含 NaN, 等长)
inline double pearson(const std::vector<double>& x, const std::vector<double>& y) {
    size_t n = x.size();
    if (n < 2 || y.size() != n) return quiet_nan();
    double mx = mean(x), my = mean(y);
    double sxy = 0.0, sxx = 0.0, syy = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double dx = x[i] - mx, dy = y[i] - my;
        sxy += dx * dy;
        sxx += dx * dx;
        syy += dy * dy;
    }
    if (sxx <= 0.0 || syy <= 0.0) return quiet_nan();  // 常数序列 -> pandas 得 NaN
    return sxy / std::sqrt(sxx * syy);
}

} // namespace detail

// 取某只股票的某个因子值, 缺失返回 NaN
inline double get_factor(const std::map<std::string, double>& row, const std::string& fname) {
    auto it = row.find(fname);
    return it == row.end() ? quiet_nan() : it->second;
}

// ===========================================================================
// 1. 因子计算 (对应 factor_lib.py::calc_factors_for_one)
// ===========================================================================

// 给定单只股票的日 K 线 (按时间升序), 返回 10 个因子 (值可能为 NaN)
// K 线不足 130 根 (6 个月动量 + buffer) 或有效日收益不足 100 个时返回空 map
// total_share: 流通股本, >0 时 TURN_20 用 "20 日均量/股本x100" (Python 同款分支)
inline std::map<std::string, double> calc_factors(const std::vector<KlineRec>& klines,
                                                  double total_share = 0.0) {
    std::map<std::string, double> f;
    const size_t n = klines.size();
    if (n < 130) return f;  // Python: len(df) < 130 -> {}

    const double NaN = quiet_nan();
    std::vector<double> close(n), volume(n), amount(n), turn(n);
    for (size_t i = 0; i < n; ++i) {
        close[i] = klines[i].close;
        volume[i] = klines[i].volume;
        amount[i] = klines[i].amount;
        turn[i] = klines[i].turnover_rate;
    }

    // 日收益率序列 (pandas close.pct_change().dropna() 等价)
    std::vector<double> rets;
    rets.reserve(n - 1);
    for (size_t i = 1; i < n; ++i) {
        if (close[i - 1] > 0.0) rets.push_back(close[i] / close[i - 1] - 1.0);
    }
    if (rets.size() < 100) return f;  // Python: len(returns) < 100 -> {}

    // 用最后收盘价对 periods 日前收盘价的 pct change (Python _safe_pct_change)
    auto safe_pct_change = [&](size_t periods) -> double {
        if (n <= periods) return NaN;
        double p_then = close[n - 1 - periods];
        if (p_then <= 0.0) return NaN;
        return close[n - 1] / p_then - 1.0;
    };

    // ---- 动量 ----
    f["MOM_1M"] = safe_pct_change(21);
    f["MOM_3M"] = safe_pct_change(63);
    f["MOM_6M"] = safe_pct_change(126);

    // ---- 反转 (短期, 取负号 -- 短期上涨过多易回调; -NaN 仍是 NaN, 天然传播) ----
    f["REV_5D"] = -safe_pct_change(5);

    // ---- 波动率 (年化, 取负号 -- 低波动好; pandas std 默认 ddof=1 样本标准差) ----
    auto annual_vol = [&](size_t win) -> double {
        double m = NaN, s = NaN;
        if (detail::mean_std(detail::tail_valid(rets, win), 1, m, s) < 2) return NaN;
        return s * std::sqrt(250.0);
    };
    f["VOL_20"] = -annual_vol(20);
    f["VOL_60"] = -annual_vol(60);

    // ---- 流动性: 20 日日均成交额对数, 取负号 (流动性好但被关注度高 -> 反向 alpha) ----
    if (amount.size() >= 20) {
        double liq = detail::mean(detail::tail_valid(amount, 20));
        // 对极小值兜底 (Python: max(liq_20, 1.0)); 全缺失 -> NaN
        f["LIQ_20"] = std::isnan(liq) ? NaN : -std::log(std::max(liq, 1.0));
    } else {
        f["LIQ_20"] = NaN;
    }

    // ---- 换手率 (取负号 -- 低换手好; 口径优先级见文件头注释) ----
    if (volume.size() >= 20) {
        double turn_20 = NaN;
        double turn_db = detail::mean(detail::tail_valid(turn, 20));
        if (!std::isnan(turn_db)) {
            // 优先: 真实换手率 (DB turnover_rate = 成交量/流通股本 x 100)
            turn_20 = turn_db;
        } else if (total_share > 0.0) {
            // Python total_share>0 分支: 20 日均量 / 股本 x 100
            turn_20 = detail::mean(detail::tail_valid(volume, 20)) / total_share * 100.0;
        } else {
            // Python total_share=0 分支: 20 日均量 / 60 日均量 相对代理
            double v20 = detail::mean(detail::tail_valid(volume, 20));
            double v60 = detail::mean(detail::tail_valid(volume, 60));
            turn_20 = (v60 > 0.0) ? v20 / v60 : NaN;
        }
        f["TURN_20"] = -turn_20;  // NaN 传播
    } else {
        f["TURN_20"] = NaN;
    }

    // ---- RSI 14 (Cutler 简单均值口径, 与 Python rolling(14).mean() 一致) ----
    double rsi_val = NaN;
    if (n > 14) {
        double gain = 0.0, loss = 0.0;
        for (size_t i = n - 14; i < n; ++i) {
            double d = close[i] - close[i - 1];
            if (d > 0.0) gain += d;
            else loss += -d;
        }
        gain /= 14.0;
        loss /= 14.0;
        // Python: loss.replace(0, nan) -> loss==0 时 rs=nan -> rsi=nan
        if (loss != 0.0) rsi_val = 100.0 - 100.0 / (1.0 + gain / loss);
    }
    // RSI 50 中性, 取 (RSI - 50) 作为强弱信号; NaN - 50 仍是 NaN
    f["RSI_14"] = rsi_val - 50.0;

    // ---- BIAS 20 (乖离率, 取负号 -- 超涨反转) ----
    double ma20 = detail::mean(detail::tail_valid(close, 20));
    double bias = (ma20 > 0.0) ? (close[n - 1] - ma20) / ma20 : NaN;
    f["BIAS_20"] = -bias;

    return f;
}

// ===========================================================================
// 2. 预处理三件套 (对应 preprocessor.py)
// ===========================================================================

// 1) MAD 去极值: 截断到 median +/- n * 1.4826 * MAD 边界 (NaN 原样保留)
//    1.4826 是高斯分布 MAD -> std 的换算系数; mad==0 时不动 (与 Python 一致)
inline std::vector<double> winsorize_mad(const std::vector<double>& values, double n = 3.0) {
    std::vector<double> out = values;
    std::vector<double> valid;
    valid.reserve(values.size());
    for (double x : values) {
        if (!std::isnan(x)) valid.push_back(x);
    }
    if (valid.empty()) return out;
    double med = detail::median(valid);
    std::vector<double> dev;
    dev.reserve(valid.size());
    for (double x : valid) dev.push_back(std::fabs(x - med));
    double mad = detail::median(dev);
    if (mad == 0.0 || std::isnan(mad)) return out;
    double upper = med + n * 1.4826 * mad;
    double lower = med - n * 1.4826 * mad;
    for (double& x : out) {
        if (std::isnan(x)) continue;
        x = std::min(std::max(x, lower), upper);
    }
    return out;
}

// 2) Z-score 标准化 (总体 std, ddof=0; NaN 原样保留)
//    std==0 时有效值全部置 0 (对应 Python 的 s*0.0)
inline std::vector<double> zscore(const std::vector<double>& values) {
    std::vector<double> out = values;
    double m = quiet_nan(), s = quiet_nan();
    if (detail::mean_std(values, 0, m, s) == 0) return out;
    if (s == 0.0 || std::isnan(s)) {
        for (double& x : out) {
            if (!std::isnan(x)) x = 0.0;
        }
        return out;
    }
    for (double& x : out) {
        if (!std::isnan(x)) x = (x - m) / s;
    }
    return out;
}

// 3) 行业中性化: 每个因子先在各行业内做 Z-score, 再做一次全市场 Z-score
//    (对应 Python industry_neutralize + 随后的 zscore 调用)
//    industry_map 中缺失 (或行业名为空) 的股票, 该期全部因子置 NaN
//    (对应 Python dropna(subset=["industry"]) 后回写对齐)
inline FactorRows neutralize_industry(const FactorRows& factor_rows,
                                      const IndustryMap& industry_map) {
    FactorRows result = factor_rows;
    for (const auto& fname : factor_names()) {
        // 按行业分组 (只保留有行业映射的股票)
        std::map<std::string, std::vector<std::string>> by_industry;
        for (const auto& kv : factor_rows) {
            auto it = industry_map.find(kv.first);
            if (it == industry_map.end() || it->second.empty()) continue;
            by_industry[it->second].push_back(kv.first);
        }
        // 行业内 Z-score
        std::map<std::string, double> neutral;  // code -> 行业内 z (可能 NaN)
        for (const auto& grp : by_industry) {
            std::vector<double> vals;
            vals.reserve(grp.second.size());
            for (const auto& code : grp.second) {
                vals.push_back(get_factor(factor_rows.at(code), fname));
            }
            std::vector<double> z = zscore(vals);
            for (size_t i = 0; i < grp.second.size(); ++i) neutral[grp.second[i]] = z[i];
        }
        // 全市场再做一次 Z-score, 让所有行业可比
        std::vector<std::string> codes;
        std::vector<double> vals;
        codes.reserve(neutral.size());
        vals.reserve(neutral.size());
        for (const auto& kv : neutral) {
            codes.push_back(kv.first);
            vals.push_back(kv.second);
        }
        std::vector<double> z2 = zscore(vals);
        std::map<std::string, double> final_z;
        for (size_t i = 0; i < codes.size(); ++i) final_z[codes[i]] = z2[i];
        // 写回: 无行业映射的股票置 NaN
        for (auto& kv : result) {
            auto it = final_z.find(kv.first);
            kv.second[fname] = (it == final_z.end()) ? quiet_nan() : it->second;
        }
    }
    return result;
}

// 完整 pipeline: 逐因子 MAD 去极值 -> Z-score -> (可选) 行业中性化
// (对应 Python preprocess_factors)
inline FactorRows preprocess(const FactorRows& factor_rows,
                             const IndustryMap& industry_map = IndustryMap(),
                             double winsorize_n = 3.0,
                             bool neutralize = true) {
    FactorRows result = factor_rows;
    for (const auto& fname : factor_names()) {
        std::vector<std::string> codes;
        std::vector<double> vals;
        codes.reserve(result.size());
        vals.reserve(result.size());
        for (const auto& kv : result) {
            codes.push_back(kv.first);
            vals.push_back(get_factor(kv.second, fname));
        }
        std::vector<double> z = zscore(winsorize_mad(vals, winsorize_n));
        for (size_t i = 0; i < codes.size(); ++i) result[codes[i]][fname] = z[i];
    }
    if (neutralize && !industry_map.empty()) {
        result = neutralize_industry(result, industry_map);
    }
    return result;
}

// ===========================================================================
// 3. 因子合成 (对应 synthesizer.py; Lasso 按任务要求不转换)
// ===========================================================================

// 等权合成: 每只股票取其有效因子 Z 分的均值 (pandas mean(axis=1) skipna)
// 全部因子缺失 -> NaN (对应 pandas 全 NaN 行, 由调用方 dropna 剔除)
inline std::map<std::string, double> combine_equal(const FactorRows& rows) {
    std::map<std::string, double> out;
    for (const auto& kv : rows) {
        double sum = 0.0;
        size_t cnt = 0;
        for (const auto& fname : factor_names()) {
            double v = get_factor(kv.second, fname);
            if (!std::isnan(v)) {
                sum += v;
                ++cnt;
            }
        }
        out[kv.first] = cnt > 0 ? sum / static_cast<double>(cnt) : quiet_nan();
    }
    return out;
}

// IC 加权合成: 权重 = IC / sum(|IC|) (ic_dict 中缺失的因子权重为 0,
// 对应 Python reindex(columns).fillna(0)); sum(|IC|)==0 时退化为等权
// NaN 因子按 0 贡献 (pandas sum(axis=1, skipna=True) 语义, 全 NaN 行得 0.0)
inline std::map<std::string, double> combine_ic_weight(
    const FactorRows& rows,
    const std::map<std::string, double>& ic_dict) {
    double abs_sum = 0.0;
    for (const auto& fname : factor_names()) {
        auto it = ic_dict.find(fname);
        if (it != ic_dict.end()) abs_sum += std::fabs(it->second);
    }
    if (abs_sum == 0.0) return combine_equal(rows);
    std::map<std::string, double> out;
    for (const auto& kv : rows) {
        double score = 0.0;
        for (const auto& fname : factor_names()) {
            double v = get_factor(kv.second, fname);
            auto it = ic_dict.find(fname);
            if (!std::isnan(v) && it != ic_dict.end()) {
                score += v * (it->second / abs_sum);
            }
        }
        out[kv.first] = score;
    }
    return out;
}

// ===========================================================================
// 4. IC / IR (对应 synthesizer.py::calc_ic / calc_ir)
// ===========================================================================

// 单期 IC: factor_values 与 forward_returns 按位置配对, 剔除任一 NaN 的对,
// 有效对 < 10 返回 NaN (与 Python 一致); method = "spearman" | "pearson"
// spearman = 秩 (并列取平均) 上的 pearson, 不受极值影响, 最常用
inline double calc_ic(const std::vector<double>& factor_values,
                      const std::vector<double>& forward_returns,
                      const std::string& method = "spearman") {
    std::vector<double> f, r;
    size_t n = std::min(factor_values.size(), forward_returns.size());
    f.reserve(n);
    r.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (std::isnan(factor_values[i]) || std::isnan(forward_returns[i])) continue;
        f.push_back(factor_values[i]);
        r.push_back(forward_returns[i]);
    }
    if (f.size() < 10) return quiet_nan();
    if (method == "pearson") return detail::pearson(f, r);
    return detail::pearson(detail::average_ranks(f), detail::average_ranks(r));
}

// Information Ratio = IC 均值 / IC 标准差 (样本 std, ddof=1)
// IR > 0.5 算优秀; 有效样本 < 2 或 std==0 返回 NaN (与 Python 一致)
inline double calc_ir(const std::vector<double>& ic_series) {
    double m = quiet_nan(), s = quiet_nan();
    if (detail::mean_std(ic_series, 1, m, s) < 2) return quiet_nan();
    return (s > 0.0) ? m / s : quiet_nan();
}

} // namespace quant::factor
