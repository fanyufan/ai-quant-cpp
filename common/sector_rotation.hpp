#pragma once

// 21-投资晨会 / CASE-B-板块轮动分析 的 C++ 转换 (header-only 纯算法库, 无 DB 依赖)
//
// 对应 Python 模块:
//   derivatives.py         -> roc_n / ma_slope_annualized / macd_hist / ma_accel / roc_accel
//   industry_strength.py   -> calc_strength (MOM_21 / RS_60 / VOL_RATIO 横截面 Z-score 等权)
//   inflection_detector.py -> Phase / detect_phase (速度投票 + 加速度投票 五相位)
//   run_today.py           -> StrengthRow / apply_composite (composite = score + phase_bonus)
//   sector_loader.py       -> build_market_benchmark (板块 close 归一化等权 x1000)
//
// 口径注意 (与 Python 对齐的关键点):
//   1. ROC_20 为小数口径 (pct_change), 不是 common/indicators.hpp 里 ind::roc 的 x100 百分数
//   2. EMA 用 pandas ewm(span, adjust=False) 递推口径: ema[0]=x[0], alpha=2/(span+1)
//   3. MACD_HIST = (DIF - DEA) x 2 (通达信约定)
//   4. Z-score 用总体标准差 (ddof=0, numpy 默认); Python pandas 版用 ddof=1,
//      但对所有板块是同一缩放因子, 不改变强度排名 (composite 因 bonus 未缩放可能有临界差异)
//   5. 市场基准日期取所有板块交易日的交集 (本库数据 131 板块日期完全对齐, 与 Python 的
//      并集 + skipna 均值完全等价); 首个 close<=0 的板块不参与归一化均值但参与 amount 求和

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace quant::sector {

// 板块日 K 线 (对应 trade_sector_daily 的合成指数 OHLC + 量额)
struct SectorBar {
    std::string date;  // YYYY-MM-DD
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    double amount = 0.0;  // 数据源无 amount 列时由调用方填 volume
};

using SectorPanel = std::map<std::string, std::vector<SectorBar>>;

inline double quiet_nan() { return std::numeric_limits<double>::quiet_NaN(); }

inline constexpr size_t kNpos = static_cast<size_t>(-1);

// ---------------------------------------------------------------
// 序列提取小工具
// ---------------------------------------------------------------

inline std::vector<double> extract_close(const std::vector<SectorBar>& bars) {
    std::vector<double> out;
    out.reserve(bars.size());
    for (const auto& b : bars) out.push_back(b.close);
    return out;
}

inline std::vector<double> extract_amount(const std::vector<SectorBar>& bars) {
    std::vector<double> out;
    out.reserve(bars.size());
    for (const auto& b : bars) out.push_back(b.amount);
    return out;
}

// ---------------------------------------------------------------
// 基础序列运算 (内部)
// ---------------------------------------------------------------

// 简单移动平均, 头部不足 period 的位置为 NaN (对齐 pandas rolling(period).mean())
inline std::vector<double> sma_nan(const std::vector<double>& x, size_t period) {
    std::vector<double> out(x.size(), quiet_nan());
    if (period == 0 || x.size() < period) return out;
    double sum = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        sum += x[i];
        if (i >= period) sum -= x[i - period];
        if (i + 1 >= period) out[i] = sum / static_cast<double>(period);
    }
    return out;
}

// 指数移动平均, pandas ewm(span, adjust=False) 口径: ema[0]=x[0], alpha=2/(span+1)
inline std::vector<double> ema_adjust_false(const std::vector<double>& x, size_t span) {
    std::vector<double> out(x.size(), quiet_nan());
    if (x.empty() || span == 0) return out;
    const double alpha = 2.0 / (static_cast<double>(span) + 1.0);
    out[0] = x[0];
    for (size_t i = 1; i < x.size(); ++i) {
        out[i] = alpha * x[i] + (1.0 - alpha) * out[i - 1];
    }
    return out;
}

// ---------------------------------------------------------------
// 一阶导 (速度类)
// ---------------------------------------------------------------

// N 日变化率 ROC (小数口径, 对齐 pandas pct_change(n)), 头部 n 个为 NaN
inline std::vector<double> roc_n(const std::vector<double>& close, size_t n = 20) {
    std::vector<double> out(close.size(), quiet_nan());
    for (size_t i = n; i < close.size(); ++i) {
        double prev = close[i - n];
        if (prev == 0.0) continue;  // 除零保护, 保持 NaN
        out[i] = (close[i] - prev) / prev;
    }
    return out;
}

inline std::vector<double> roc_n(const std::vector<SectorBar>& bars, size_t n = 20) {
    return roc_n(extract_close(bars), n);
}

// MA(ma_period) 上 slope_window 日窗口最小二乘斜率, 年化为 %/年 (假设 252 交易日)
// 年化公式: slope * 252 / |y_mean| * 100, 与 Python derivatives.ma_slope 完全一致
inline std::vector<double> ma_slope_annualized(const std::vector<double>& close,
                                               size_t ma_period = 20,
                                               size_t slope_window = 10) {
    const size_t len = close.size();
    std::vector<double> out(len, quiet_nan());
    if (ma_period == 0 || slope_window < 2 || len < ma_period + slope_window - 1) return out;

    const std::vector<double> ma = sma_nan(close, ma_period);
    const double w = static_cast<double>(slope_window);
    const double x_mean = (w - 1.0) / 2.0;
    double denom = 0.0;  // sum (x - x_mean)^2
    for (size_t k = 0; k < slope_window; ++k) {
        const double d = static_cast<double>(k) - x_mean;
        denom += d * d;
    }
    if (denom == 0.0) return out;

    for (size_t i = ma_period + slope_window - 2; i < len; ++i) {
        const size_t base = i - slope_window + 1;  // 窗口 ma[base .. i]
        double y_mean = 0.0;
        bool bad = false;
        for (size_t k = 0; k < slope_window; ++k) {
            const double y = ma[base + k];
            if (std::isnan(y)) { bad = true; break; }  // 窗口含 NaN -> NaN (同 Python)
            y_mean += y;
        }
        if (bad) continue;
        y_mean /= w;
        if (y_mean == 0.0) continue;
        double num = 0.0;  // sum (x - x_mean)(y - y_mean)
        for (size_t k = 0; k < slope_window; ++k) {
            num += (static_cast<double>(k) - x_mean) * (ma[base + k] - y_mean);
        }
        const double slope = num / denom;
        out[i] = slope * 252.0 / std::fabs(y_mean) * 100.0;  // 单位 %
    }
    return out;
}

// ---------------------------------------------------------------
// 二阶导 (加速度类)
// ---------------------------------------------------------------

struct MacdResult {
    std::vector<double> dif;
    std::vector<double> dea;
    std::vector<double> hist;  // (DIF - DEA) x 2, 通达信约定
};

// 经典 MACD: DIF=EMA12-EMA26, DEA=EMA9(DIF), HIST=(DIF-DEA)x2 (全长无 NaN)
inline MacdResult macd_hist(const std::vector<double>& close,
                            size_t fast = 12, size_t slow = 26, size_t signal = 9) {
    MacdResult r;
    const std::vector<double> ema_fast = ema_adjust_false(close, fast);
    const std::vector<double> ema_slow = ema_adjust_false(close, slow);
    r.dif.resize(close.size());
    for (size_t i = 0; i < close.size(); ++i) r.dif[i] = ema_fast[i] - ema_slow[i];
    r.dea = ema_adjust_false(r.dif, signal);
    r.hist.resize(close.size());
    for (size_t i = 0; i < close.size(); ++i) r.hist[i] = (r.dif[i] - r.dea[i]) * 2.0;
    return r;
}

// MA 斜率的二阶导: 当前斜率 - lag 日前斜率 (单位与 ma_slope_annualized 一致)
inline std::vector<double> ma_accel(const std::vector<double>& close,
                                    size_t ma_period = 20, size_t slope_window = 10,
                                    size_t lag = 5) {
    const std::vector<double> slope = ma_slope_annualized(close, ma_period, slope_window);
    std::vector<double> out(close.size(), quiet_nan());
    for (size_t i = lag; i < close.size(); ++i) {
        if (std::isnan(slope[i]) || std::isnan(slope[i - lag])) continue;
        out[i] = slope[i] - slope[i - lag];
    }
    return out;
}

// ROC 的二阶导: 当前 ROC - lag 日前 ROC
inline std::vector<double> roc_accel(const std::vector<double>& close,
                                     size_t roc_days = 20, size_t lag = 5) {
    const std::vector<double> roc = roc_n(close, roc_days);
    std::vector<double> out(close.size(), quiet_nan());
    for (size_t i = lag; i < close.size(); ++i) {
        if (std::isnan(roc[i]) || std::isnan(roc[i - lag])) continue;
        out[i] = roc[i] - roc[i - lag];
    }
    return out;
}

// ---------------------------------------------------------------
// 拐点检测: 五相位模型 (inflection_detector.py)
// ---------------------------------------------------------------

enum class Phase {
    ACCEL_UP,    // 加速上涨 / 强势确认 (v>0, a>0)
    DECEL_UP,    // 减速上涨 / 撤出预警 (v>0, a<0)
    ACCEL_DOWN,  // 加速下跌 / 杀跌确认 (v<0, a<0)
    DECEL_DOWN,  // 减速下跌 / 见底信号 (v<0, a>0)
    NEUTRAL      // 震荡 / 信号不明
};

inline const char* phase_to_string(Phase p) {
    switch (p) {
        case Phase::ACCEL_UP: return "accel_up";
        case Phase::DECEL_UP: return "decel_up";
        case Phase::ACCEL_DOWN: return "accel_down";
        case Phase::DECEL_DOWN: return "decel_down";
        case Phase::NEUTRAL: return "neutral";
    }
    return "neutral";
}

inline const char* phase_to_desc(Phase p) {
    switch (p) {
        case Phase::ACCEL_UP: return "加速上涨 / 强势确认";
        case Phase::DECEL_UP: return "减速上涨 / 撤出预警";
        case Phase::ACCEL_DOWN: return "加速下跌 / 杀跌确认";
        case Phase::DECEL_DOWN: return "减速下跌 / 见底信号";
        case Phase::NEUTRAL: return "震荡 / 信号不明";
    }
    return "震荡 / 信号不明";
}

// 相位推荐分 (run_today.py PHASE_BONUS): 主升 +3, 见底 +2, 高位钝化 +0.5, 主跌 -2, 中性 0
inline double phase_bonus(Phase p) {
    switch (p) {
        case Phase::ACCEL_UP: return 3.0;
        case Phase::DECEL_DOWN: return 2.0;
        case Phase::DECEL_UP: return 0.5;
        case Phase::ACCEL_DOWN: return -2.0;
        case Phase::NEUTRAL: return 0.0;
    }
    return 0.0;
}

// 分组展示顺序 (Python phase_order): accel_up -> decel_up -> decel_down -> accel_down -> neutral
inline const std::vector<Phase>& phase_display_order() {
    static const std::vector<Phase> order = {
        Phase::ACCEL_UP, Phase::DECEL_UP, Phase::DECEL_DOWN, Phase::ACCEL_DOWN, Phase::NEUTRAL};
    return order;
}

// 带阈值的符号函数: NaN -> 0, x>thr -> +1, x<-thr -> -1, 否则 0 (同 Python _sign)
inline int sign_with_threshold(double x, double threshold) {
    if (std::isnan(x)) return 0;
    if (x > threshold) return 1;
    if (x < -threshold) return -1;
    return 0;
}

struct PhaseInfo {
    Phase phase = Phase::NEUTRAL;
    int vote_velocity = 0;             // 速度组投票方向 (+1/-1/0)
    int vote_accel = 0;                // 加速度组投票方向 (+1/-1/0)
    double roc_20 = quiet_nan();       // 小数口径
    double ma20_slope = quiet_nan();   // 年化 %
    double macd_hist = quiet_nan();
    double ma20_accel = quiet_nan();
    double hist_delta = quiet_nan();   // MACD_HIST 日差分
    bool valid = true;                 // false = 样本不足 (<30 根), 调用方按 neutral 处理
};

// 单板块相位判定 (detect_one_sector_phase):
//   速度组:   v = sign(ROC_20, roc_threshold)*2 + sign(MA20_SLOPE, accel_threshold)*1
//   加速度组: a = sign(MA20_ACCEL, accel_threshold) + sign(MACD_HIST 日差分, accel_threshold)
//   (v>0,a>0)->accel_up, (v>0,a<0)->decel_up, (v<0,a<0)->accel_down,
//   (v<0,a>0)->decel_down, 其他->neutral
// 注意: close 应传入截止日的全部历史 (Python 用全量 close 算导数, 不是 lookback 窗口)
inline PhaseInfo detect_phase(const std::vector<double>& close,
                              double roc_threshold = 0.005,    // 0.5%, 过滤几乎平盘的噪音
                              double accel_threshold = 0.0) {  // 加速度已是差值, 0 附近当中性
    PhaseInfo info;
    if (close.size() < 30) {
        info.valid = false;
        return info;
    }
    const std::vector<double> roc20 = roc_n(close, 20);
    const std::vector<double> slope = ma_slope_annualized(close, 20, 10);
    const MacdResult macd = macd_hist(close);
    const std::vector<double> accel = ma_accel(close, 20, 10, 5);

    const size_t last = close.size() - 1;
    const size_t prev = last - 1;

    info.roc_20 = roc20[last];
    info.ma20_slope = slope[last];
    info.macd_hist = macd.hist[last];
    info.ma20_accel = accel[last];
    info.hist_delta = macd.hist[last] - macd.hist[prev];

    const int v_score = sign_with_threshold(info.roc_20, roc_threshold) * 2
                        + sign_with_threshold(info.ma20_slope, accel_threshold) * 1;
    info.vote_velocity = (v_score > 0) ? 1 : ((v_score < 0) ? -1 : 0);

    const int a_score = sign_with_threshold(info.ma20_accel, accel_threshold) * 1
                        + sign_with_threshold(info.hist_delta, accel_threshold) * 1;
    info.vote_accel = (a_score > 0) ? 1 : ((a_score < 0) ? -1 : 0);

    if (info.vote_velocity > 0 && info.vote_accel > 0) {
        info.phase = Phase::ACCEL_UP;
    } else if (info.vote_velocity > 0 && info.vote_accel < 0) {
        info.phase = Phase::DECEL_UP;
    } else if (info.vote_velocity < 0 && info.vote_accel < 0) {
        info.phase = Phase::ACCEL_DOWN;
    } else if (info.vote_velocity < 0 && info.vote_accel > 0) {
        info.phase = Phase::DECEL_DOWN;
    } else {
        info.phase = Phase::NEUTRAL;
    }
    return info;
}

inline PhaseInfo detect_phase(const std::vector<SectorBar>& bars,
                              double roc_threshold = 0.005,
                              double accel_threshold = 0.0) {
    return detect_phase(extract_close(bars), roc_threshold, accel_threshold);
}

// ---------------------------------------------------------------
// 横截面 Z-score (总体标准差 ddof=0, numpy 默认; std=0 时全 0 防除零)
// ---------------------------------------------------------------

inline std::vector<double> zscore(const std::vector<double>& x) {
    std::vector<double> z(x.size(), 0.0);
    if (x.empty()) return z;
    const double n = static_cast<double>(x.size());
    double mu = 0.0;
    for (double v : x) mu += v;
    mu /= n;
    double var = 0.0;
    for (double v : x) {
        const double d = v - mu;
        var += d * d;
    }
    const double sd = std::sqrt(var / n);
    if (sd <= 0.0) return z;  // 除零保护: 所有 z 为 0
    for (size_t i = 0; i < x.size(); ++i) z[i] = (x[i] - mu) / sd;
    return z;
}

// ---------------------------------------------------------------
// 市场基准 (sector_loader.build_market_benchmark):
//   各板块 close 除以自身首日 close 归一化, 在日期交集上等权平均 x 1000
//   amount 为各板块成交额之和
// ---------------------------------------------------------------

struct Benchmark {
    std::vector<std::string> dates;  // 交集交易日, 升序
    std::vector<double> close;       // 等权归一化 x1000
    std::vector<double> amount;      // 各板块 amount 之和
    bool empty() const { return dates.empty(); }
    size_t size() const { return dates.size(); }
};

inline Benchmark build_market_benchmark(const SectorPanel& all_sectors) {
    Benchmark bench;
    if (all_sectors.empty()) return bench;

    // 统计每个交易日出现在多少个板块中, 取交集 (count == 板块总数)
    std::map<std::string, size_t> date_count;
    for (const auto& kv : all_sectors) {
        for (const auto& b : kv.second) ++date_count[b.date];
    }
    const size_t n_sectors = all_sectors.size();

    // 每板块 date -> bar 下标, 便于 O(1) 对齐
    std::vector<const std::vector<SectorBar>*> panel;
    std::vector<std::unordered_map<std::string, size_t>> lookups;
    panel.reserve(n_sectors);
    lookups.reserve(n_sectors);
    for (const auto& kv : all_sectors) {
        panel.push_back(&kv.second);
        std::unordered_map<std::string, size_t> idx;
        idx.reserve(kv.second.size());
        for (size_t i = 0; i < kv.second.size(); ++i) idx[kv.second[i].date] = i;
        lookups.push_back(std::move(idx));
    }

    for (const auto& dc : date_count) {  // std::map 按日期升序迭代
        if (dc.second != n_sectors) continue;  // 只保留交集中的日期
        const std::string& date = dc.first;
        double sum_norm = 0.0;
        size_t norm_cnt = 0;
        double sum_amount = 0.0;
        for (size_t s = 0; s < n_sectors; ++s) {
            const std::vector<SectorBar>& bars = *panel[s];
            const SectorBar& bar = bars[lookups[s][date]];
            const double base = bars.front().close;  // 归一化基准: 板块自身首日 close
            if (base > 0.0) {  // 首日 close<=0 的板块不参与均值 (同 Python)
                sum_norm += bar.close / base;
                ++norm_cnt;
            }
            sum_amount += bar.amount;  // amount 求和不剔除 (同 Python)
        }
        if (norm_cnt == 0) continue;
        bench.dates.push_back(date);
        bench.close.push_back(sum_norm / static_cast<double>(norm_cnt) * 1000.0);
        bench.amount.push_back(sum_amount);
    }
    return bench;
}

// 在基准日期中找最后一个小于等于 end_date 的下标; end_date 为空取最新; 找不到返回 kNpos
inline size_t find_end_idx(const Benchmark& bench, const std::string& end_date) {
    if (bench.empty()) return kNpos;
    if (end_date.empty()) return bench.size() - 1;
    size_t result = kNpos;
    for (size_t i = 0; i < bench.size(); ++i) {
        if (bench.dates[i] <= end_date) {
            result = i;
        } else {
            break;
        }
    }
    return result;
}

// ---------------------------------------------------------------
// 板块强度排名 (industry_strength.py)
// ---------------------------------------------------------------

struct StrengthRow {
    std::string sector;
    double score = 0.0;      // 三指标 Z-score 等权均值
    double mom_21 = 0.0;     // 21 日动量 (小数)
    double rs_60 = 0.0;      // 60 日相对强度 (小数)
    double vol_ratio = 0.0;  // 5 日均额 / 60 日均额
    Phase phase = Phase::NEUTRAL;
    double composite = 0.0;  // score + phase_bonus
    int rank = 0;            // calc_strength 后为强度排名(min 法), apply_composite 后为综合排名
};

// 横截面强度打分: 对 end_idx 截止日, 取各板块最后 max(lookback_days, 70) 根 K 线,
// 与基准按日期对齐 (交集), 至少 70 个共同交易日才参与:
//   MOM_21    = close[-1] / close[-22] - 1
//   RS_60     = (close[-1]/close[-61] - 1) - (bench[-1]/bench[-61] - 1)
//   VOL_RATIO = mean(amount[-5:]) / mean(amount[-60:])
// 三指标横截面 Z-score 等权合成 score, 按 score 降序返回, rank 为强度排名 (并列取最小名次)
inline std::vector<StrengthRow> calc_strength(const SectorPanel& all_sectors,
                                              const Benchmark& benchmark,
                                              size_t end_idx,
                                              size_t lookback_days = 90) {
    std::vector<StrengthRow> rows;
    if (benchmark.empty() || end_idx >= benchmark.size()) return rows;
    const std::string& end_date = benchmark.dates[end_idx];
    const size_t window = std::max<size_t>(lookback_days, 70);

    // 基准 date -> 下标 (只用到 end_idx 为止)
    std::unordered_map<std::string, size_t> bench_idx;
    bench_idx.reserve(end_idx + 1);
    for (size_t i = 0; i <= end_idx; ++i) bench_idx[benchmark.dates[i]] = i;

    for (const auto& kv : all_sectors) {
        const std::vector<SectorBar>& bars = kv.second;  // 已按日期升序
        // 有效长度: 日期 <= end_date 的 bar 数
        size_t avail = 0;
        while (avail < bars.size() && bars[avail].date <= end_date) ++avail;
        if (avail < 70) continue;  // Python: len(df_window) < 70 -> 跳过
        const size_t start = (avail > window) ? (avail - window) : 0;

        // 与基准按日期取交集对齐 (Python: common = ind.index.intersection(bench.index))
        std::vector<double> ind_close, ind_amount, bench_close;
        ind_close.reserve(window);
        ind_amount.reserve(window);
        bench_close.reserve(window);
        for (size_t i = start; i < avail; ++i) {
            const auto it = bench_idx.find(bars[i].date);
            if (it == bench_idx.end()) continue;
            ind_close.push_back(bars[i].close);
            ind_amount.push_back(bars[i].amount);
            bench_close.push_back(benchmark.close[it->second]);
        }
        const size_t len = ind_close.size();
        if (len < 70) continue;  // Python: len(common) < 70 -> 跳过

        const double mom_21 = ind_close[len - 1] / ind_close[len - 22] - 1.0;
        const double ind_ret = ind_close[len - 1] / ind_close[len - 61] - 1.0;
        const double bench_ret = bench_close[len - 1] / bench_close[len - 61] - 1.0;
        const double rs_60 = ind_ret - bench_ret;

        double v5 = 0.0, v60 = 0.0;
        for (size_t i = len - 5; i < len; ++i) v5 += ind_amount[i];
        v5 /= 5.0;
        for (size_t i = len - 60; i < len; ++i) v60 += ind_amount[i];
        v60 /= 60.0;
        const double vol_ratio = (v60 > 0.0) ? (v5 / v60) : quiet_nan();

        // Python dropna: 任一指标 NaN/inf 的板块不参与排名
        if (!std::isfinite(mom_21) || !std::isfinite(rs_60) || !std::isfinite(vol_ratio)) continue;

        StrengthRow row;
        row.sector = kv.first;
        row.mom_21 = mom_21;
        row.rs_60 = rs_60;
        row.vol_ratio = vol_ratio;
        rows.push_back(row);
    }

    if (rows.empty()) return rows;

    // 横截面 Z-score 等权合成
    std::vector<double> mom_v, rs_v, vr_v;
    mom_v.reserve(rows.size());
    rs_v.reserve(rows.size());
    vr_v.reserve(rows.size());
    for (const auto& r : rows) {
        mom_v.push_back(r.mom_21);
        rs_v.push_back(r.rs_60);
        vr_v.push_back(r.vol_ratio);
    }
    const std::vector<double> zm = zscore(mom_v);
    const std::vector<double> zr = zscore(rs_v);
    const std::vector<double> zv = zscore(vr_v);
    for (size_t i = 0; i < rows.size(); ++i) {
        rows[i].score = (zm[i] + zr[i] + zv[i]) / 3.0;
    }

    // 强度排名: 降序, 并列取最小名次 (pandas rank(ascending=False, method="min"))
    std::vector<size_t> order(rows.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return rows[a].score > rows[b].score; });
    int cur_rank = 1;
    for (size_t pos = 0; pos < order.size(); ++pos) {
        if (pos > 0 && rows[order[pos]].score < rows[order[pos - 1]].score) {
            cur_rank = static_cast<int>(pos) + 1;
        }
        rows[order[pos]].rank = cur_rank;
    }

    // 按 score 降序返回 (Python sort_values("score", ascending=False))
    std::stable_sort(rows.begin(), rows.end(),
                     [](const StrengthRow& a, const StrengthRow& b) { return a.score > b.score; });
    return rows;
}

// 综合评分 (run_today.py): composite = score + phase_bonus, 降序重排, rank = 综合排名 1..N
// 调用前需先填好每行的 phase (strength 左 join phase, 缺失按 neutral 处理)
inline void apply_composite(std::vector<StrengthRow>& rows) {
    for (auto& r : rows) r.composite = r.score + phase_bonus(r.phase);
    std::stable_sort(rows.begin(), rows.end(),
                     [](const StrengthRow& a, const StrengthRow& b) {
                         return a.composite > b.composite;
                     });
    for (size_t i = 0; i < rows.size(); ++i) rows[i].rank = static_cast<int>(i) + 1;
}

}  // namespace quant::sector
