#pragma once

// Kris 风控引擎公共头文件
// 对应 week10/19-强化学习与风控体系-20260418/CASE-Kris的风控体系/1-风控引擎.py
// 跳过 EventLLMChecker（依赖大模型），保留关键词事件检查。

#include <algorithm>
#include <cmath>
#include <ctime>

#include <fmt/format.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quant::risk {

// ============================================================
// 基础数据结构
// ============================================================

enum class Decision {
    APPROVE, // 通过
    WARN,    // 警告
    REJECT,  // 拒绝
    HALT     // 熔断
};

inline std::string decision_name(Decision d) {
    switch (d) {
        case Decision::APPROVE: return "approve";
        case Decision::WARN: return "warn";
        case Decision::REJECT: return "reject";
        case Decision::HALT: return "halt";
    }
    return "unknown";
}

inline std::string decision_icon(Decision d) {
    switch (d) {
        case Decision::APPROVE: return "[PASS]";
        case Decision::WARN: return "[WARN]";
        case Decision::REJECT: return "[REJECT]";
        case Decision::HALT: return "[HALT]";
    }
    return "[??]";
}

inline int decision_severity(Decision d) {
    switch (d) {
        case Decision::HALT: return 4;
        case Decision::REJECT: return 3;
        case Decision::WARN: return 2;
        case Decision::APPROVE: return 1;
    }
    return 0;
}

inline std::string current_timestamp() {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

struct RiskDecision {
    Decision decision = Decision::APPROVE;
    std::string reason;
    std::string rule_name;
    double max_position_pct = 1.0;
    std::string timestamp = current_timestamp();

    bool is_approved() const { return decision == Decision::APPROVE || decision == Decision::WARN; }
    bool is_rejected() const { return decision == Decision::REJECT || decision == Decision::HALT; }

    std::string to_string() const {
        return fmt_icon() + " " + rule_name + ": " + reason;
    }

    std::string fmt_icon() const { return decision_icon(decision); }
};

struct Order {
    std::string stock_code;
    std::string direction; // "buy" / "sell"
    double amount = 0.0;
    double price = 0.0;
    int quantity = 0;
    std::string timestamp = current_timestamp();

    Order() = default;
    Order(std::string code, std::string dir, double amt, double prc, int qty = 0)
        : stock_code(std::move(code)), direction(std::move(dir)), amount(amt), price(prc), quantity(qty) {
        if (quantity == 0 && price > 0.0) {
            quantity = static_cast<int>(amount / price / 100.0) * 100;
        }
    }
};

// ============================================================
// 事前预防
// ============================================================

struct PreTradeConfig {
    double max_order_amount = 200'000.0;
    double price_collar_pct = 0.05;
    std::vector<std::string> blacklist;
    double atr_risk_pct = 0.01;
    double atr_overshoot_ratio = 2.0;
};

class PreTradeGuard {
public:
    explicit PreTradeGuard(const PreTradeConfig& cfg = {}) : cfg_(cfg) {
        for (const auto& s : cfg.blacklist) blacklist_.insert(s);
    }

    std::vector<RiskDecision> check_all(const Order& order,
                                        const std::unordered_map<std::string, double>& prices,
                                        const std::unordered_map<std::string, double>& atr,
                                        double total_asset) {
        std::vector<RiskDecision> results;
        results.push_back(check_order_amount(order));
        results.push_back(check_price_collar(order, prices));
        results.push_back(check_blacklist(order));
        auto atr_result = check_atr_position(order, atr, total_asset);
        if (atr_result.rule_name == "ATR仓位检查") results.push_back(atr_result);
        return results;
    }

    RiskDecision check_order_amount(const Order& order) const {
        if (order.amount > cfg_.max_order_amount) {
            return {Decision::REJECT,
                    fmt_amount(order.amount) + " 超过上限 " + fmt_amount(cfg_.max_order_amount),
                    "单笔金额上限"};
        }
        return {Decision::APPROVE,
                fmt_amount(order.amount) + " 在限额内",
                "单笔金额上限"};
    }

    RiskDecision check_price_collar(const Order& order,
                                    const std::unordered_map<std::string, double>& prices) const {
        auto it = prices.find(order.stock_code);
        if (it == prices.end() || it->second <= 0.0) {
            return {Decision::APPROVE, "无现价信息, 跳过价格偏离检查", "价格偏离检查"};
        }
        double current_price = it->second;
        double deviation = std::abs(order.price - current_price) / current_price;
        if (deviation > cfg_.price_collar_pct) {
            return {Decision::REJECT,
                    fmt_price(order.price) + " 偏离现价 " + fmt_price(current_price) + " 达 " +
                        fmt_pct(deviation) + ", 超过 " + fmt_pct(cfg_.price_collar_pct) + " 限制",
                    "价格偏离检查"};
        }
        return {Decision::APPROVE, "价格偏离 " + fmt_pct(deviation) + ", 正常", "价格偏离检查"};
    }

    RiskDecision check_blacklist(const Order& order) const {
        bool is_st = order.stock_code.find("ST") != std::string::npos;
        if (is_st || blacklist_.count(order.stock_code)) {
            return {Decision::REJECT, order.stock_code + " 命中 ST/黑名单, 禁止买入", "ST黑名单"};
        }
        return {Decision::APPROVE, order.stock_code + " 不在黑名单", "ST黑名单"};
    }

    RiskDecision check_atr_position(const Order& order,
                                    const std::unordered_map<std::string, double>& atr,
                                    double total_asset) const {
        auto it = atr.find(order.stock_code);
        if (it == atr.end() || it->second <= 0.0 || order.direction != "buy") {
            return {Decision::APPROVE, "", ""};
        }
        double atr_value = it->second;
        if (total_asset <= 0.0) return {Decision::APPROVE, "", ""};

        double suggested_position = (total_asset * cfg_.atr_risk_pct) / atr_value * order.price;
        double atr_stop_price = order.price - 2.0 * atr_value;
        if (order.amount > suggested_position * cfg_.atr_overshoot_ratio) {
            return {Decision::WARN,
                    "ATR=" + fmt_price(atr_value) + ", 建议仓位 " + fmt_amount(suggested_position) +
                        "元, 实际 " + fmt_amount(order.amount) + "元 (超 " +
                        fmt_ratio(order.amount / suggested_position) + "倍). ATR止损价: " +
                        fmt_price(atr_stop_price),
                    "ATR仓位检查",
                    suggested_position / order.amount};
        }
        return {Decision::APPROVE,
                "ATR=" + fmt_price(atr_value) + ", 建议仓位 " + fmt_amount(suggested_position) +
                    "元, 实际 " + fmt_amount(order.amount) + "元, 风险可控. ATR止损参考: " +
                    fmt_price(atr_stop_price),
                "ATR仓位检查"};
    }

private:
    PreTradeConfig cfg_;
    std::unordered_set<std::string> blacklist_;

    static std::string fmt_amount(double v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(0) << v;
        return oss.str();
    }
    static std::string fmt_price(double v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << v;
        return oss.str();
    }
    static std::string fmt_pct(double v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << v * 100.0 << "%";
        return oss.str();
    }
    static std::string fmt_ratio(double v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << v;
        return oss.str();
    }
};

// ============================================================
// 事中熔断
// ============================================================

struct CircuitBreakerConfig {
    double max_daily_loss_pct = 0.02;
    double atr_stop_multiplier = 2.0;
};

struct AtrStopInfo {
    double entry_price = 0.0;
    double atr = 0.0;
    double stop_price = 0.0;
};

class CircuitBreaker {
public:
    explicit CircuitBreaker(const CircuitBreakerConfig& cfg = {}) : cfg_(cfg) {}

    void reset_daily(double start_nav) {
        daily_start_nav_ = start_nav;
        current_nav_ = start_nav;
        is_halted_ = false;
        halt_reason_.clear();
    }

    RiskDecision update_nav(double nav) {
        current_nav_ = nav;
        if (daily_start_nav_ <= 0.0) return {Decision::APPROVE, "", ""};
        double loss_pct = (daily_start_nav_ - nav) / daily_start_nav_;
        if (loss_pct >= cfg_.max_daily_loss_pct) {
            is_halted_ = true;
            halt_reason_ = "单日亏损 " + fmt_pct(loss_pct) + " 触发熔断线 " + fmt_pct(cfg_.max_daily_loss_pct);
            return {Decision::HALT, halt_reason_, "单日亏损熔断"};
        }
        return {Decision::APPROVE, "", ""};
    }

    void register_position(const std::string& stock_code, double entry_price, double atr) {
        if (atr <= 0.0) return;
        atr_stops_[stock_code] = {entry_price, atr, entry_price - cfg_.atr_stop_multiplier * atr};
    }

    void remove_position(const std::string& stock_code) { atr_stops_.erase(stock_code); }

    RiskDecision check_atr_stop(const std::string& stock_code, double current_price) const {
        auto it = atr_stops_.find(stock_code);
        if (it == atr_stops_.end()) return {Decision::APPROVE, "", ""};
        const auto& info = it->second;
        if (current_price <= info.stop_price) {
            return {Decision::REJECT,
                    stock_code + " 触发ATR止损: 入场价 " + fmt_price(info.entry_price) +
                        ", ATR=" + fmt_price(info.atr) + ", 止损价 " + fmt_price(info.stop_price) +
                        ", 现价 " + fmt_price(current_price),
                    "ATR止损"};
        }
        return {Decision::APPROVE, "", ""};
    }

    bool is_halted() const { return is_halted_; }
    const std::string& halt_reason() const { return halt_reason_; }

    struct Status {
        double daily_start_nav = 0.0;
        double current_nav = 0.0;
        double daily_pnl_pct = 0.0;
        bool is_halted = false;
        std::string halt_reason;
        std::unordered_map<std::string, AtrStopInfo> atr_stops;
    };
    Status get_status() const {
        Status s;
        s.daily_start_nav = daily_start_nav_;
        s.current_nav = current_nav_;
        if (daily_start_nav_ > 0.0) s.daily_pnl_pct = (current_nav_ - daily_start_nav_) / daily_start_nav_;
        s.is_halted = is_halted_;
        s.halt_reason = halt_reason_;
        s.atr_stops = atr_stops_;
        return s;
    }

private:
    CircuitBreakerConfig cfg_;
    double daily_start_nav_ = 0.0;
    double current_nav_ = 0.0;
    bool is_halted_ = false;
    std::string halt_reason_;
    std::unordered_map<std::string, AtrStopInfo> atr_stops_;

    static std::string fmt_pct(double v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << v * 100.0 << "%";
        return oss.str();
    }
    static std::string fmt_price(double v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3) << v;
        return oss.str();
    }
};

// ============================================================
// 外部信号
// ============================================================

class EventKeywordChecker {
public:
    explicit EventKeywordChecker(const std::vector<std::string>& keywords = default_keywords()) {
        keywords_ = keywords.empty() ? default_keywords() : keywords;
    }

    RiskDecision check(const std::string& stock_code, const std::string& news_text) const {
        if (news_text.empty()) {
            return {Decision::APPROVE, stock_code + " 无近期新闻, 跳过", "事件关键词"};
        }
        for (const auto& kw : keywords_) {
            if (news_text.find(kw) != std::string::npos) {
                return {Decision::REJECT, stock_code + " 新闻命中重大利空: " + kw, "事件关键词"};
            }
        }
        return {Decision::APPROVE, stock_code + " 新闻未发现重大利空", "事件关键词"};
    }

    static std::vector<std::string> default_keywords() {
        return {"退市", "暂停上市", "终止上市", "*ST", "立案调查", "立案侦查",
                "行政处罚", "涉嫌违法", "涉嫌犯罪", "财务造假"};
    }

private:
    std::vector<std::string> keywords_;
};

class MacroGate {
public:
    double update_vix(double vix) {
        current_vix_ = vix;
        has_vix_ = true;
        if (vix >= 50.0) {
            position_coefficient_ = 0.0;
            risk_level_ = "末日级别";
        } else if (vix >= 35.0) {
            position_coefficient_ = 0.30 - (vix - 35.0) / 15.0 * 0.20;
            risk_level_ = "极度恐慌";
        } else if (vix >= 25.0) {
            position_coefficient_ = 0.70 - (vix - 25.0) / 10.0 * 0.40;
            risk_level_ = "恐慌";
        } else if (vix >= 20.0) {
            position_coefficient_ = 1.00 - (vix - 20.0) / 5.0 * 0.30;
            risk_level_ = "焦虑";
        } else {
            position_coefficient_ = 1.0;
            risk_level_ = "正常";
        }
        return position_coefficient_;
    }

    RiskDecision check() const {
        if (!has_vix_) {
            return {Decision::APPROVE, "未提供 VIX, 跳过宏观门控", "宏观VIX门控"};
        }
        if (position_coefficient_ <= 0.0) {
            return {Decision::HALT,
                    "VIX=" + fmt_vix() + " 极度恐慌, 暂停所有开仓",
                    "宏观VIX门控",
                    0.0};
        }
        if (position_coefficient_ < 1.0) {
            return {Decision::WARN,
                    "VIX=" + fmt_vix() + " (" + risk_level_ + "), 仓位系数降至 " + fmt_pct(),
                    "宏观VIX门控",
                    position_coefficient_};
        }
        return {Decision::APPROVE,
                "VIX=" + fmt_vix() + " 正常, 仓位系数 100%",
                "宏观VIX门控"};
    }

    double current_vix() const { return current_vix_; }
    double coefficient() const { return position_coefficient_; }
    const std::string& risk_level() const { return risk_level_; }

private:
    bool has_vix_ = false;
    double current_vix_ = 0.0;
    double position_coefficient_ = 1.0;
    std::string risk_level_ = "未知";

    std::string fmt_vix() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << current_vix_;
        return oss.str();
    }
    std::string fmt_pct() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(0) << position_coefficient_ * 100.0 << "%";
        return oss.str();
    }
};

// ============================================================
// 统一入口：RiskManager
// ============================================================

struct RiskManagerConfig {
    PreTradeConfig pre_trade;
    CircuitBreakerConfig circuit_breaker;
    std::vector<std::string> event_keywords;
};

struct AuditEntry {
    std::string time;
    std::string stock;
    std::string direction;
    double amount = 0.0;
    std::string decision;
    std::string rule;
    std::string reason;
};

class RiskManager {
public:
    explicit RiskManager(const RiskManagerConfig& cfg = {})
        : pre_trade_(cfg.pre_trade),
          circuit_breaker_(cfg.circuit_breaker),
          event_(cfg.event_keywords.empty() ? EventKeywordChecker::default_keywords()
                                            : cfg.event_keywords) {}

    void start_day(double start_nav) { circuit_breaker_.reset_daily(start_nav); }

    RiskDecision approve(const Order& order,
                         const std::unordered_map<std::string, double>& prices,
                         const std::unordered_map<std::string, double>& atr,
                         double total_asset,
                         const std::string& news_text = "") {
        return approve_with_details(order, prices, atr, total_asset, news_text).back();
    }

    std::vector<RiskDecision> approve_with_details(
        const Order& order,
        const std::unordered_map<std::string, double>& prices,
        const std::unordered_map<std::string, double>& atr,
        double total_asset,
        const std::string& news_text = "") {
        // 1) 已熔断
        if (circuit_breaker_.is_halted()) {
            RiskDecision d{Decision::HALT,
                           "交易已熔断: " + circuit_breaker_.halt_reason(),
                           "熔断状态"};
            log(order, d);
            return {d};
        }

        // 2) 宏观门控
        auto macro_d = macro_.check();
        if (macro_d.decision == Decision::HALT) {
            log(order, macro_d);
            return {macro_d};
        }

        std::vector<RiskDecision> all_checks = {macro_d};

        // 3) 事件关键词（仅买入）
        if (order.direction == "buy") {
            auto event_d = event_.check(order.stock_code, news_text);
            all_checks.push_back(event_d);
            if (event_d.decision == Decision::REJECT) {
                auto final = get_strictest(all_checks);
                log(order, final);
                all_checks.push_back(final);
                return all_checks;
            }
        }

        // 4) 事前检查
        auto pre = pre_trade_.check_all(order, prices, atr, total_asset);
        all_checks.insert(all_checks.end(), pre.begin(), pre.end());

        // 5) 最严决策
        auto final = get_strictest(all_checks);
        log(order, final);
        all_checks.push_back(final);
        return all_checks;
    }

    RiskDecision on_trade_complete(double nav) { return circuit_breaker_.update_nav(nav); }

    void register_position(const std::string& stock_code, double entry_price, double atr) {
        circuit_breaker_.register_position(stock_code, entry_price, atr);
    }

    void remove_position(const std::string& stock_code) { circuit_breaker_.remove_position(stock_code); }

    RiskDecision check_atr_stop(const std::string& stock_code, double current_price) const {
        return circuit_breaker_.check_atr_stop(stock_code, current_price);
    }

    const std::vector<AuditEntry>& audit_log() const { return audit_log_; }

    void print_audit_log(size_t last_n = 20) const {
        fmt::print("\n{0}\n", std::string(70, '='));
        fmt::print("  Kris 审批日志 (最近 {} 条)\n", std::min(last_n, audit_log_.size()));
        fmt::print("{0}\n", std::string(70, '='));
        for (size_t i = audit_log_.size() > last_n ? audit_log_.size() - last_n : 0;
             i < audit_log_.size(); ++i) {
            const auto& e = audit_log_[i];
            std::string tag = (e.decision == "approve" ? "OK" : (e.decision == "warn" ? "!!" : "XX"));
            fmt::print("  [{}] {} | {} {} {:>10.0f} | {}: {}\n",
                       tag, e.time, e.stock, e.direction, e.amount, e.rule,
                       e.reason.size() > 60 ? e.reason.substr(0, 60) + "..." : e.reason);
        }
        fmt::print("{0}\n", std::string(70, '='));
    }

    struct Summary {
        size_t total = 0;
        size_t approved = 0;
        size_t warned = 0;
        size_t rejected = 0;
        double rejection_rate = 0.0;
        CircuitBreaker::Status circuit_breaker;
        struct MacroInfo {
            double vix = 0.0;
            double coefficient = 1.0;
            std::string risk_level;
        } macro;
    };

    Summary get_summary() const {
        Summary s;
        s.total = audit_log_.size();
        for (const auto& e : audit_log_) {
            if (e.decision == "approve") ++s.approved;
            else if (e.decision == "warn") ++s.warned;
            else if (e.decision == "reject" || e.decision == "halt") ++s.rejected;
        }
        s.rejection_rate = s.total > 0 ? static_cast<double>(s.rejected) / s.total : 0.0;
        s.circuit_breaker = circuit_breaker_.get_status();
        s.macro.vix = macro_.current_vix();
        s.macro.coefficient = macro_.coefficient();
        s.macro.risk_level = macro_.risk_level();
        return s;
    }

    MacroGate& macro() { return macro_; }
    const MacroGate& macro() const { return macro_; }

private:
    PreTradeGuard pre_trade_;
    CircuitBreaker circuit_breaker_;
    EventKeywordChecker event_;
    MacroGate macro_;
    std::vector<AuditEntry> audit_log_;

    void log(const Order& order, const RiskDecision& d) {
        audit_log_.push_back({d.timestamp, order.stock_code, order.direction, order.amount,
                              decision_name(d.decision), d.rule_name, d.reason});
    }

    RiskDecision get_strictest(const std::vector<RiskDecision>& decisions) const {
        auto strictest = *std::max_element(decisions.begin(), decisions.end(),
                                           [](const RiskDecision& a, const RiskDecision& b) {
                                               return decision_severity(a.decision) <
                                                      decision_severity(b.decision);
                                           });
        std::vector<RiskDecision> rejections;
        std::vector<RiskDecision> warnings;
        for (const auto& d : decisions) {
            if (d.is_rejected()) rejections.push_back(d);
            else if (d.decision == Decision::WARN) warnings.push_back(d);
        }
        if (!rejections.empty()) {
            std::string reasons;
            for (size_t i = 0; i < rejections.size(); ++i) {
                if (i > 0) reasons += "; ";
                reasons += "[" + rejections[i].rule_name + "] " + rejections[i].reason;
            }
            return {strictest.decision, reasons, "综合审批"};
        }
        if (!warnings.empty()) {
            double min_pct = warnings[0].max_position_pct;
            for (const auto& w : warnings) min_pct = std::min(min_pct, w.max_position_pct);
            std::string reasons;
            for (size_t i = 0; i < warnings.size(); ++i) {
                if (i > 0) reasons += "; ";
                reasons += "[" + warnings[i].rule_name + "] " + warnings[i].reason;
            }
            return {Decision::WARN, reasons, "综合审批", min_pct};
        }
        return {Decision::APPROVE, "所有检查通过", "综合审批"};
    }
};

} // namespace quant::risk
