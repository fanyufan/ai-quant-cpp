#pragma once

// ============================================================================
// 龙头战法 (Dragon Picker) -- A 股化首板战法核心逻辑
// header-only, 纯逻辑, 无 DB 依赖
//
// 移植自:
//   week11/22-实盘作战与CEO控制台-20260429/CASE-龙头战法/dragon_strategy/dragon_picker.py
//
// 理论基础: Ross Cameron 的美股 Gap and Go 策略 A 股本土化
//   (A 股 ±10% 涨跌停限制, 不能直接照搬美股涨幅 > 10% 的口径)
//
//   v1 5 大筛选法则:
//     1. 当日涨幅 > min_change        动量效应, 已涨的票短期延续性更高
//     2. 涨幅榜前 top_n_scan 名       集中度 + 流动性
//     3. 流通市值 [mcap_low, mcap_high] 太大筹码沉淀, 太小易被庄家拉抬
//     4. 量比 > min_volume_ratio      有量才是真涨, 没量的假突破会被打回
//     5. 排除 ST / 退市
//   v2 硬规则补丁 (避免实战漏洞):
//     6. 涨幅 < max_change            涨停板/一字板买不到, T+1 高开污染回测统计
//     7. 上市天数 >= min_listed_days  排除次新股 (波动巨大 + 形态不可信)
//        注意: listed_days 是【自然日】, 与 Python (t - list_date).days 一致
//     8. 板块共振: 板块当日涨幅 >= 0.5% 且上涨家数占比 >= 40%
//        板块数据缺失时【直接淘汰】, 不放过孤雁 (孤雁难成龙)
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quant::dragon {

// v2-8 板块共振硬阈值 (与 Python SECTOR_MIN_CHANGE_PCT / SECTOR_MIN_RISE_RATIO 一致)
inline constexpr double SECTOR_MIN_CHANGE_PCT = 0.005;  // 板块当日涨幅 >= 0.5%
inline constexpr double SECTOR_MIN_RISE_RATIO = 0.40;   // 板块上涨家数占比 >= 40%

// ----------------------------------------------------------------------------
// 一只股票的当日输入 (对应 Python 的 stock_data dict)
// ----------------------------------------------------------------------------
struct DragonStockInput {
    std::string code;
    std::string name;
    double day_change_pct = 0.0;      // 当日涨幅 (小数, 0.072 = +7.2%)
    double price = 0.0;               // 当前价 (回测口径 = T 日收盘价)
    double volume_ratio = 0.0;        // 量比 = T 量 / 前 5 日均量
    double float_market_cap = 0.0;    // 流通市值 (元) = float_shares * close_T
    int listed_days = -1;             // 上市天数 (自然日); -1 表示未知 (等价 Python None)
    std::string sector_1;             // 一级板块 (回测按 sector_1 汇总用)
    std::string sector_2;             // 二级板块 (板块共振查 trade_sector_daily 用)
    std::optional<double> sector_change_pct;  // 所在 sector_2 当日涨幅 (小数)
    std::optional<double> sector_rise_ratio;  // 板块上涨家数占比 (0-1)

    // ---- filter 过程填入 ----
    int rank_in_top = 0;              // 当日涨幅榜排名 (1 起)
    double dragon_score = 0.0;        // 龙头综合分 (幸存者才有)
};

// ----------------------------------------------------------------------------
// 筛选配置 (默认值与 Python filter_dragon_candidates 一致)
// ----------------------------------------------------------------------------
struct DragonFilterConfig {
    double min_change = 0.05;             // v1-1 涨幅下限
    double max_change = 0.095;            // v2-6 涨幅上限 (排除近涨停)
    double max_price = 30.0;              // 价格上限 (元)
    double mcap_low = 30e8;               // 流通市值下限 (元)
    double mcap_high = 500e8;             // 流通市值上限 (元)
    double min_volume_ratio = 2.0;        // v1-4 量比下限
    int min_listed_days = 60;             // v2-7 上市天数下限 (自然日)
    bool require_sector_resonance = true; // v2-8 板块共振硬过滤开关
    int top_n_scan = 50;                  // v1-2 只看涨幅榜前 N 名
};

// ----------------------------------------------------------------------------
// 筛选结果: 幸存者 + 被拦截明细 (每条带原因字符串, 复盘用)
// ----------------------------------------------------------------------------
struct DragonFilterResult {
    std::vector<DragonStockInput> candidates;  // 幸存者, 已按 dragon_score 降序
    std::vector<std::pair<DragonStockInput, std::string>> rejected;  // (股票, 拦截原因)
};

namespace detail {

inline double round3(double x) { return std::round(x * 1000.0) / 1000.0; }

// 保留 2 位小数的数字字符串 (避免在头文件里依赖 fmt)
inline std::string num2(double x) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", x);
    return buf;
}

// 百分数字符串: 0.072 -> "+7.20%"
inline std::string pct2(double x) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%+.2f%%", x * 100.0);
    return buf;
}

// 仅 ASCII 大写化 (用于 ST 检测; UTF-8 中文字节 >= 0x80 不受影响)
inline std::string upper_ascii(const std::string& s) {
    std::string out = s;
    for (auto& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

} // namespace detail

// ----------------------------------------------------------------------------
// 板块共振加分 (0 ~ 1.5)
//   - 板块涨幅 0.5% -> 0 分; 1% -> 0.2; 3% -> 1.0 (满)
//   - 板块上涨家数占比 50% -> 0 分; 70% -> 0.5 (满)
//   板块字段缺失 (例如 sector_2 为空) 时不加不减
// ----------------------------------------------------------------------------
inline double calc_sector_resonance_score(const DragonStockInput& s) {
    if (!s.sector_change_pct && !s.sector_rise_ratio) return 0.0;
    double score = 0.0;
    if (s.sector_change_pct && *s.sector_change_pct > 0.005) {
        score += std::min((*s.sector_change_pct - 0.005) / 0.025, 1.0);
    }
    if (s.sector_rise_ratio && *s.sector_rise_ratio > 0.5) {
        score += std::min((*s.sector_rise_ratio - 0.5) / 0.2, 1.0) * 0.5;
    }
    return detail::round3(score);
}

// ----------------------------------------------------------------------------
// 板块共振硬过滤: 板块涨幅 + 上涨家数占比都达标才放行
// 没有板块数据 (sector_2 为空 / 当日无板块行情) 直接淘汰, 不放过孤雁
// ----------------------------------------------------------------------------
inline bool passes_sector_resonance(const DragonStockInput& s) {
    if (!s.sector_change_pct || !s.sector_rise_ratio) return false;
    return *s.sector_change_pct >= SECTOR_MIN_CHANGE_PCT &&
           *s.sector_rise_ratio >= SECTOR_MIN_RISE_RATIO;
}

// ----------------------------------------------------------------------------
// 龙头综合分 -- v1 5 法则打分 + v2 板块共振加分
// ----------------------------------------------------------------------------
inline double calc_dragon_score(const DragonStockInput& s) {
    double score = 0.0;

    // 涨幅: 越大越好 (但 > 9% 边际效应递减, 容易封板, 难买)
    if (s.day_change_pct > 0.09) {
        score += 0.5;  // 接近涨停, 减分 (买不到 + 第二天高开)
    } else {
        score += std::min(s.day_change_pct * 10.0, 1.0);  // 5% -> 0.5, 8% -> 0.8
    }

    // 量比: 越大越好, 但 > 8 容易是异常
    score += std::min(s.volume_ratio / 5.0, 1.5);

    // 流通市值: 50-200 亿最佳
    const double mcap = s.float_market_cap;
    if (mcap >= 50e8 && mcap <= 200e8) {
        score += 1.0;
    } else if ((mcap >= 30e8 && mcap < 50e8) || (mcap > 200e8 && mcap <= 500e8)) {
        score += 0.5;
    }

    // 涨幅榜排名: 越靠前越好
    if (s.rank_in_top <= 5) {
        score += 1.0;
    } else if (s.rank_in_top <= 20) {
        score += 0.5;
    } else if (s.rank_in_top <= 50) {
        score += 0.2;
    }

    // 价格: < 20 加分, 20-30 中性, > 30 减分
    if (s.price < 20.0) {
        score += 0.5;
    } else if (s.price <= 30.0) {
        score += 0.2;
    }

    // v2 新增: 板块共振加分 (站在强势板块里 + 板块内多家齐涨, 才像真龙)
    score += calc_sector_resonance_score(s);

    return detail::round3(score);
}

// ----------------------------------------------------------------------------
// ST / 退市检测 (v1-5): 名称含 ST / * / 退
// ----------------------------------------------------------------------------
inline bool is_st_or_delisting(const std::string& name) {
    const std::string up = detail::upper_ascii(name);
    return up.find("ST") != std::string::npos ||
           up.find('*') != std::string::npos ||
           name.find("退") != std::string::npos;  // "退" UTF-8 子串匹配即可
}

// ----------------------------------------------------------------------------
// 应用 v1 5 大筛选法则 + v2 硬规则补丁 + 板块共振硬过滤, 返回候选 + 拦截明细
//
// 与 Python filter_dragon_candidates 口径一致:
//   1) 全市场按涨幅降序稳定排序, 编 rank_in_top (1 起), 只看前 top_n_scan 名
//   2) 逐条硬过滤 (顺序与 Python 相同):
//        v1-1 涨幅 >= min_change
//        v2-6 涨幅 <= max_change
//        价格 <= max_price
//        v1-3 流通市值在 [mcap_low, mcap_high]
//        v1-4 量比 >= min_volume_ratio
//        v1-5 排除 ST / 退市
//        v2-7 上市天数 >= min_listed_days (listed_days < 0 视为未知, 放过)
//        v2-8 板块共振 (require_sector_resonance=true 时)
//   3) 幸存者算 dragon_score, 按分数降序
//
// 输入按值传入 (函数内排序/写字段, 不影响调用方原始数据, 便于 v1/v2 对照各跑一遍)
// ----------------------------------------------------------------------------
inline DragonFilterResult filter_dragon_candidates(std::vector<DragonStockInput> stocks,
                                                   const DragonFilterConfig& cfg = {}) {
    DragonFilterResult result;

    // 涨幅降序 (稳定排序, 与 Python sorted 一致), 编涨幅榜名次 (1 起)
    std::stable_sort(stocks.begin(), stocks.end(),
                     [](const DragonStockInput& a, const DragonStockInput& b) {
                         return a.day_change_pct > b.day_change_pct;
                     });
    for (size_t i = 0; i < stocks.size(); ++i) {
        stocks[i].rank_in_top = static_cast<int>(i) + 1;
    }

    for (auto& s : stocks) {
        std::string reason;

        // v1-2: 只看涨幅榜前 N 名
        if (s.rank_in_top > cfg.top_n_scan) {
            reason = "v1-2 涨幅榜前 " + std::to_string(cfg.top_n_scan) + " 名之外";
        } else if (s.day_change_pct < cfg.min_change) {
            // v1 法则 1: 涨幅不足
            reason = "v1-1 涨幅 " + detail::pct2(s.day_change_pct) + " < " +
                     detail::pct2(cfg.min_change);
        } else if (s.day_change_pct > cfg.max_change) {
            // v2 法则 6: 接近涨停的不收 (涨停板买不到, T+1 高开污染统计)
            reason = "v2-6 涨幅 " + detail::pct2(s.day_change_pct) + " 接近涨停 (上限 " +
                     detail::pct2(cfg.max_change) + ")";
        } else if (s.price > cfg.max_price) {
            // v1 价格法则: 低价股波动性更强, 容易被散户追捧
            reason = "v1 价格 " + detail::num2(s.price) + " 元 > " +
                     detail::num2(cfg.max_price) + " 元上限";
        } else if (s.float_market_cap < cfg.mcap_low || s.float_market_cap > cfg.mcap_high) {
            // v1 法则 3: 流通市值适中
            reason = "v1-3 流通市值 " + detail::num2(s.float_market_cap / 1e8) +
                     " 亿不在 [" + detail::num2(cfg.mcap_low / 1e8) + ", " +
                     detail::num2(cfg.mcap_high / 1e8) + "] 亿";
        } else if (s.volume_ratio < cfg.min_volume_ratio) {
            // v1 法则 4: 量比不足
            reason = "v1-4 量比 " + detail::num2(s.volume_ratio) + " < " +
                     detail::num2(cfg.min_volume_ratio);
        } else if (is_st_or_delisting(s.name)) {
            // v1 法则 5: 排除 ST / 退市
            reason = "v1-5 ST/退市股 (" + s.name + ")";
        } else if (s.listed_days >= 0 && s.listed_days < cfg.min_listed_days) {
            // v2 法则 7: 排除次新股 (listed_days < 0 视为未知, 与 Python None 一样放过)
            reason = "v2-7 上市 " + std::to_string(s.listed_days) + " 天 < " +
                     std::to_string(cfg.min_listed_days) + " 天 (次新股)";
        } else if (cfg.require_sector_resonance && !passes_sector_resonance(s)) {
            // v2 法则 8: 板块共振硬过滤
            if (!s.sector_change_pct || !s.sector_rise_ratio) {
                reason = "v2-8 板块数据缺失, 孤雁不龙 (sector_2=" +
                         (s.sector_2.empty() ? std::string("(空)") : s.sector_2) + ")";
            } else {
                reason = "v2-8 板块共振不足 (板块 " + detail::pct2(*s.sector_change_pct) +
                         " / 上涨占比 " + detail::pct2(*s.sector_rise_ratio) + ", 需 >= " +
                         detail::pct2(SECTOR_MIN_CHANGE_PCT) + " / " +
                         detail::pct2(SECTOR_MIN_RISE_RATIO) + ")";
            }
        }

        if (!reason.empty()) {
            result.rejected.emplace_back(s, std::move(reason));
            continue;
        }

        // 幸存者: 算分 (v1 5 项 + v2 共振加分)
        s.dragon_score = calc_dragon_score(s);
        result.candidates.push_back(s);
    }

    // 按分数降序 (稳定, 与 Python 一致)
    std::stable_sort(result.candidates.begin(), result.candidates.end(),
                     [](const DragonStockInput& a, const DragonStockInput& b) {
                         return a.dragon_score > b.dragon_score;
                     });
    return result;
}

// ----------------------------------------------------------------------------
// 入场出场参数 (对应 Python DragonEntryExit.calc_entry 返回的 dict)
// ----------------------------------------------------------------------------
struct DragonEntryParams {
    std::string code;
    std::string name;
    double entry_price = 0.0;   // 限价入场价
    double stop_loss = 0.0;     // 止损价
    double target = 0.0;        // 止盈价 (盈亏比 2:1)
    int quantity = 0;           // 股数 (整百)
    double amount = 0.0;        // 买入金额 (元)
    double max_loss = 0.0;      // 最大亏损 (元)
    double max_gain = 0.0;      // 最大盈利 (元)
    int max_hold_minutes = 0;   // 最长持仓分钟数
    double payoff_ratio = 2.0;  // 盈亏比
};

// ----------------------------------------------------------------------------
// 龙头战法的入场出场规则
//
// 核心铁律 (Ross Cameron 总结):
//   1. Base Hit 小赢 -- 每股目标 0.3-0.8 元, 不追求本垒打
//   2. 盈亏比 >= 2:1
//   3. 单笔最大亏损 <= 总资金 x 1%
//   4. 时间止损 -- 持仓不过当日 (或最长 N 分钟)
// ----------------------------------------------------------------------------
class DragonEntryExit {
public:
    explicit DragonEntryExit(double capital = 1'000'000.0,
                             double risk_per_trade_pct = 0.01,  // 单笔风险 1%
                             double target_payoff_ratio = 2.0,
                             int max_hold_minutes = 120,        // 最长持有 2 小时
                             double daily_max_loss_pct = 0.02)  // 日亏损上限 2%
        : capital_(capital),
          risk_per_trade_pct_(risk_per_trade_pct),
          target_payoff_ratio_(target_payoff_ratio),
          max_hold_minutes_(max_hold_minutes),
          daily_max_loss_pct_(daily_max_loss_pct) {}

    double capital() const { return capital_; }
    double risk_per_trade_pct() const { return risk_per_trade_pct_; }
    double target_payoff_ratio() const { return target_payoff_ratio_; }
    int max_hold_minutes() const { return max_hold_minutes_; }
    double daily_max_loss_pct() const { return daily_max_loss_pct_; }

    // 给一只候选股算入场参数 (与 Python calc_entry 逐步一致)
    DragonEntryParams calc_entry(const DragonStockInput& candidate) const {
        DragonEntryParams p;
        p.code = candidate.code;
        p.name = candidate.name;
        p.max_hold_minutes = max_hold_minutes_;
        p.payoff_ratio = target_payoff_ratio_;

        const double price = candidate.price;
        if (price <= 0.0) return p;  // 防御: 价格非法直接返回空参数

        // 止损位 = 入场价 - 1 个 ATR (简化为 2% 价格波动; 实战应接真 ATR)
        const double atr_pct = 0.02;
        const double stop_pct = atr_pct;
        const double target_pct = stop_pct * target_payoff_ratio_;

        // 股数 = 风险预算 / 每股亏损, 向下取整百; 至少 1 手试探
        const double max_loss_amount = capital_ * risk_per_trade_pct_;
        const double per_share_loss = price * stop_pct;
        int quantity = static_cast<int>(max_loss_amount / per_share_loss / 100.0) * 100;
        if (quantity == 0) quantity = 100;
        // 单笔金额不超过总资金 5% (与 Python 一致: 高价股可能被压到 0 股)
        const double max_amount = capital_ * 0.05;
        if (quantity * price > max_amount) {
            quantity = static_cast<int>(max_amount / price / 100.0) * 100;
        }

        auto r2 = [](double x) { return std::round(x * 100.0) / 100.0; };
        p.entry_price = detail::round3(price);
        p.stop_loss = detail::round3(price * (1.0 - stop_pct));
        p.target = detail::round3(price * (1.0 + target_pct));
        p.quantity = quantity;
        p.amount = r2(quantity * price);
        p.max_loss = r2(quantity * price * stop_pct);
        p.max_gain = r2(quantity * price * target_pct);
        return p;
    }

private:
    double capital_;
    double risk_per_trade_pct_;
    double target_payoff_ratio_;
    int max_hold_minutes_;
    double daily_max_loss_pct_;
};

// ----------------------------------------------------------------------------
// 日期工具 (自然日计算, listed_days / 窗口平移用)
// ----------------------------------------------------------------------------

// 解析 "YYYY-MM-DD" 或 "YYYYMMDD"
inline bool parse_date_ymd(const std::string& s, int& y, int& m, int& d) {
    int digits = 0;
    for (char c : s) {
        if (c >= '0' && c <= '9') ++digits;
        else if (c != '-' && c != '/') return false;
    }
    if (digits != 8) return false;
    std::string ds;
    for (char c : s) {
        if (c >= '0' && c <= '9') ds.push_back(c);
    }
    y = std::stoi(ds.substr(0, 4));
    m = std::stoi(ds.substr(4, 2));
    d = std::stoi(ds.substr(6, 2));
    return m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

// Howard Hinnant days-from-civil: 公元纪日 -> 序列日
inline long long days_from_civil(int y, int m, int d) {
    y -= (m <= 2) ? 1 : 0;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy =
        (153 * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

// days_from_civil 的逆运算
inline void civil_from_days(long long z, int& y, int& m, int& d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int yy = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp + (mp < 10 ? 3 : -9));
    y = yy + (m <= 2 ? 1 : 0);
}

// to - from 的自然日差 (listed_days 口径); 任一解析失败返回 -1
inline int calendar_days_between(const std::string& from, const std::string& to) {
    int y1, m1, d1, y2, m2, d2;
    if (!parse_date_ymd(from, y1, m1, d1) || !parse_date_ymd(to, y2, m2, d2)) return -1;
    return static_cast<int>(days_from_civil(y2, m2, d2) - days_from_civil(y1, m1, d1));
}

// 日期平移 n 天 (正数向后, 负数向前), 返回 "YYYY-MM-DD"; 解析失败返回 ""
inline std::string shift_date(const std::string& date, int days) {
    int y, m, d;
    if (!parse_date_ymd(date, y, m, d)) return "";
    civil_from_days(days_from_civil(y, m, d) + days, y, m, d);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
    return buf;
}

// ----------------------------------------------------------------------------
// 构造 12 只 "今日候选股" mock 数据 (与 Python _build_mock_today_stocks 完全一致)
// 教学意图: 让 demo 既出 2-3 只通过的 "真龙", 也展示 v2 把哪些 v1 能进的拦掉
// 实战这一步要从 xtdata / MySQL 拉当日真实涨跌幅 + 量比 + 板块行情
// ----------------------------------------------------------------------------
inline std::vector<DragonStockInput> build_mock_today_stocks() {
    std::vector<DragonStockInput> v;

    auto add = [&v](const std::string& code, const std::string& name, double chg,
                    double price, double vr, double mcap, const std::string& sector_2,
                    double s_chg, double s_rise, int listed_days) {
        DragonStockInput s;
        s.code = code;
        s.name = name;
        s.day_change_pct = chg;
        s.price = price;
        s.volume_ratio = vr;
        s.float_market_cap = mcap;
        s.sector_2 = sector_2;
        s.sector_change_pct = s_chg;
        s.sector_rise_ratio = s_rise;
        s.listed_days = listed_days;
        v.push_back(s);
    };

    // ===== v2 通过的 "真龙" (板块强 + 涨幅适中 + 30-500 亿) =====
    // 强势板块: 半导体 +3.2%, 中盘股
    add("688981.SH", "中芯国际", 0.072, 28.6, 4.8, 320e8, "半导体", 0.032, 0.70, 1200);
    // 强势板块: 锂电 +2.8%
    add("300014.SZ", "亿纬锂能", 0.068, 25.8, 3.6, 280e8, "电池", 0.028, 0.65, 3800);
    // 中等板块, 但形态完美
    add("300059.SZ", "东方财富", 0.063, 18.6, 3.2, 180e8, "证券", 0.018, 0.58, 4000);
    add("002241.SZ", "歌尔股份", 0.058, 21.3, 3.8, 150e8, "消费电子", 0.022, 0.62, 4200);

    // ===== v1 能进, v2 各种法则会拦的 "诱多" =====
    // 板块没共振 (医疗信息化 +0.3% < 0.5%), v2-8 拦
    add("300253.SZ", "卫宁健康", 0.085, 8.7, 5.2, 195e8, "医疗信息化", 0.003, 0.35, 3200);
    // 接近涨停 9.7%, v2-6 拦
    add("603799.SH", "华友钴业", 0.097, 28.4, 6.5, 480e8, "小金属", 0.020, 0.60, 2800);
    // 次新股 (上市 30 天), v2-7 拦
    add("301999.SZ", "次新示例", 0.072, 22.0, 4.2, 90e8, "半导体", 0.032, 0.70, 30);
    // 板块负 (白酒 -0.5%); 实际涨幅 1.8% < 5% v1-1 已拦
    add("600519.SH", "贵州茅台", 0.018, 1407.2, 1.4, 17000e8, "白酒", -0.005, 0.30, 5500);

    // ===== 各种 v1 直接淘汰的 (大盘股 / ST / 量比小) =====
    // 大盘股 v1-3 拦
    add("300750.SZ", "宁德时代", 0.072, 285.6, 3.5, 1250e8, "电池", 0.028, 0.65, 2200);
    // 价格 > 30, v1 max_price 拦
    add("002460.SZ", "赣锋锂业", 0.056, 32.1, 2.8, 470e8, "电池", 0.028, 0.65, 3500);
    // 量比小, v1-4 拦
    add("600438.SH", "通威股份", 0.061, 17.5, 1.6, 290e8, "光伏设备", 0.025, 0.60, 3800);
    // ST, v1-5 拦
    add("000001.SZ", "*ST 测试", 0.099, 4.2, 8.0, 80e8, "其他", 0.010, 0.50, 2000);

    return v;
}

} // namespace quant::dragon
