// 19-强化学习与风控体系 / CASE-Kris的风控体系/1-风控引擎.py 的 C++ 实现
// 命令行单笔订单审批 + 内置演示用例

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "risk_engine.hpp"

using json = nlohmann::json;

namespace {

void print_report(const std::vector<quant::risk::RiskDecision>& decisions,
                  const quant::risk::Order& order,
                  const quant::risk::RiskManager::Summary& summary) {
    fmt::print("\n{0}\n", std::string(70, '='));
    fmt::print("  Kris 风控审批 -- {} {}\n", order.stock_code, order.direction);
    fmt::print("{0}\n", std::string(70, '='));
    fmt::print("  订单: 金额 {:.0f} 元, 价格 {:.3f}, 数量 {}\n",
               order.amount, order.price, order.quantity);
    for (const auto& d : decisions) {
        if (d.rule_name.empty()) continue;
        fmt::print("  {}\n", d.to_string());
    }
    fmt::print("{0}\n", std::string(70, '='));
    fmt::print("  统计: 共 {} 笔, 通过 {}, 警告 {}, 拒绝/熔断 {}, 拒绝率 {:.1f}%\n",
               summary.total, summary.approved, summary.warned, summary.rejected,
               summary.rejection_rate * 100.0);
}

json to_json(const std::vector<quant::risk::RiskDecision>& decisions,
             const quant::risk::Order& order,
             const quant::risk::RiskManager::Summary& summary) {
    json j;
    j["order"] = {{"stock_code", order.stock_code},
                  {"direction", order.direction},
                  {"amount", order.amount},
                  {"price", order.price},
                  {"quantity", order.quantity}};
    json checks = json::array();
    for (const auto& d : decisions) {
        if (d.rule_name.empty()) continue;
        json c;
        c["rule"] = d.rule_name;
        c["decision"] = quant::risk::decision_name(d.decision);
        c["reason"] = d.reason;
        c["max_position_pct"] = d.max_position_pct;
        checks.push_back(c);
    }
    j["checks"] = checks;
    if (!decisions.empty()) {
        j["final_decision"] = quant::risk::decision_name(decisions.back().decision);
        j["final_reason"] = decisions.back().reason;
    }
    j["summary"] = {{"total", summary.total},
                    {"approved", summary.approved},
                    {"warned", summary.warned},
                    {"rejected", summary.rejected},
                    {"rejection_rate_pct", std::round(summary.rejection_rate * 1000.0) / 10.0}};
    j["macro"] = {{"vix", summary.macro.vix},
                  {"coefficient", summary.macro.coefficient},
                  {"risk_level", summary.macro.risk_level}};
    return j;
}

void save_json(const std::string& path, const json& j) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return;
    ofs << j.dump(2);
    fmt::print("  JSON 已保存: {}\n", path);
}

void run_demo() {
    fmt::print("{0}\n", std::string(70, '='));
    fmt::print("  Kris 风控引擎 -- 内置 8 条规则演示\n");
    fmt::print("{0}\n", std::string(70, '='));

    quant::risk::RiskManagerConfig cfg;
    cfg.pre_trade.max_order_amount = 200'000.0;
    cfg.pre_trade.price_collar_pct = 0.05;
    cfg.circuit_breaker.max_daily_loss_pct = 0.02;

    quant::risk::RiskManager kris(cfg);
    kris.start_day(1'000'000.0);
    kris.macro().update_vix(18.5);

    std::unordered_map<std::string, double> prices = {
        {"510050.SH", 3.00}, {"600519.SH", 1700.0}};
    std::unordered_map<std::string, double> atr = {
        {"510050.SH", 0.05}, {"600519.SH", 30.0}};
    double total_asset = 1'000'000.0;

    struct TestCase {
        std::string desc;
        quant::risk::Order order;
        std::string news;
    };
    std::vector<TestCase> cases = {
        {"正常订单", {"510050.SH", "buy", 100'000.0, 3.00}, "50ETF成交活跃, 资金面平稳"},
        {"超限额", {"510050.SH", "buy", 500'000.0, 3.00}, ""},
        {"价格偏离 (fat finger)", {"510050.SH", "buy", 50'000.0, 3.40}, ""},
        {"ST 黑名单", {"000001.SZ_ST", "buy", 30'000.0, 10.0}, ""},
        {"ATR 仓位超标", {"600519.SH", "buy", 800'000.0, 1700.0}, "茅台业绩稳定增长"},
        {"事件利空 (立案调查)", {"600519.SH", "buy", 50'000.0, 1700.0}, "据悉, 公司昨日被证监会立案调查"},
    };

    for (const auto& tc : cases) {
        fmt::print("\n--- {} ---\n", tc.desc);
        auto decisions = kris.approve_with_details(tc.order, prices, atr, total_asset, tc.news);
        for (const auto& d : decisions) {
            if (!d.rule_name.empty()) fmt::print("  {}\n", d.to_string());
        }
    }

    // 事中熔断
    fmt::print("\n--- 净值跌到 970,000 (-3%) ---\n");
    auto halt = kris.on_trade_complete(970'000.0);
    if (halt.rule_name == "单日亏损熔断") {
        fmt::print("  {}\n", halt.to_string());
    }

    // 熔断后再下单
    fmt::print("\n--- 熔断后再下单 ---\n");
    auto decisions = kris.approve_with_details({"510050.SH", "buy", 50'000.0, 3.00},
                                                prices, atr, total_asset, "");
    for (const auto& d : decisions) {
        if (!d.rule_name.empty()) fmt::print("  {}\n", d.to_string());
    }

    // 重启日，VIX=42
    fmt::print("\n--- 重启日, VIX=42 极度恐慌 ---\n");
    kris.start_day(970'000.0);
    kris.macro().update_vix(42.0);
    decisions = kris.approve_with_details({"510050.SH", "buy", 50'000.0, 3.00},
                                           prices, atr, 970'000.0, "");
    for (const auto& d : decisions) {
        if (!d.rule_name.empty()) fmt::print("  {}\n", d.to_string());
    }

    kris.print_audit_log(20);
    auto summary = kris.get_summary();
    fmt::print("\n统计: 共 {} 笔, 通过 {}, 警告 {}, 拒绝/熔断 {}, 拒绝率 {:.1f}%\n",
               summary.total, summary.approved, summary.warned, summary.rejected,
               summary.rejection_rate * 100.0);
}

} // namespace

int main(int argc, char* argv[]) {
    // Default: run demo. If --code provided, run single order approval.
    std::string code;
    std::string direction = "buy";
    double amount = 100'000.0;
    double price = 0.0;
    int quantity = 0;
    double total_asset = 1'000'000.0;
    double vix = 18.5;
    double current_price = 0.0;
    double atr_value = 0.0;
    std::string news;
    double max_order_amount = 200'000.0;
    double price_collar_pct = 0.05;
    double max_daily_loss_pct = 0.02;
    std::string output_json;
    bool demo = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--code" || arg == "-c") && i + 1 < argc) code = argv[++i];
        else if (arg == "--direction" && i + 1 < argc) direction = argv[++i];
        else if (arg == "--amount" && i + 1 < argc) amount = std::stod(argv[++i]);
        else if (arg == "--price" && i + 1 < argc) price = std::stod(argv[++i]);
        else if (arg == "--quantity" && i + 1 < argc) quantity = std::atoi(argv[++i]);
        else if (arg == "--total_asset" && i + 1 < argc) total_asset = std::stod(argv[++i]);
        else if (arg == "--vix" && i + 1 < argc) vix = std::stod(argv[++i]);
        else if (arg == "--current_price" && i + 1 < argc) current_price = std::stod(argv[++i]);
        else if (arg == "--atr" && i + 1 < argc) atr_value = std::stod(argv[++i]);
        else if (arg == "--news" && i + 1 < argc) news = argv[++i];
        else if (arg == "--max_order_amount" && i + 1 < argc) max_order_amount = std::stod(argv[++i]);
        else if (arg == "--price_collar_pct" && i + 1 < argc) price_collar_pct = std::stod(argv[++i]);
        else if (arg == "--max_daily_loss_pct" && i + 1 < argc) max_daily_loss_pct = std::stod(argv[++i]);
        else if (arg == "--output" && i + 1 < argc) output_json = argv[++i];
        else if (arg == "--demo") demo = true;
    }

    std::filesystem::create_directories("outputs/week10");

    if (demo || code.empty()) {
        run_demo();
        fmt::print("\n[结果] {{\"status\": \"success\", \"mode\": \"demo\"}}\n");
        return 0;
    }

    if (price <= 0.0) {
        fmt::print("[错误] 必须提供 --price\n");
        return 1;
    }

    quant::risk::RiskManagerConfig cfg;
    cfg.pre_trade.max_order_amount = max_order_amount;
    cfg.pre_trade.price_collar_pct = price_collar_pct;
    cfg.circuit_breaker.max_daily_loss_pct = max_daily_loss_pct;

    quant::risk::RiskManager kris(cfg);
    kris.start_day(total_asset);
    kris.macro().update_vix(vix);

    quant::risk::Order order(code, direction, amount, price, quantity);
    std::unordered_map<std::string, double> prices;
    std::unordered_map<std::string, double> atr;
    if (current_price > 0.0) prices[code] = current_price;
    if (atr_value > 0.0) atr[code] = atr_value;

    auto decisions = kris.approve_with_details(order, prices, atr, total_asset, news);
    auto summary = kris.get_summary();
    print_report(decisions, order, summary);

    auto j = to_json(decisions, order, summary);
    if (!output_json.empty()) save_json(output_json, j);
    else save_json("outputs/week10/kris_risk_engine.json", j);

    fmt::print("\n[结果] {{\"status\": \"success\", \"decision\": \"{}\", \"code\": \"{}\"}}\n",
               quant::risk::decision_name(decisions.back().decision), code);
    return 0;
}
