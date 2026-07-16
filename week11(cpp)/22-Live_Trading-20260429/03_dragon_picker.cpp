// 22-实盘作战与CEO控制台 / CASE-龙头战法/dragon_strategy/dragon_picker.py 的 C++ 实现
// 龙头战法选股 CLI:
//   无参默认   : 跑 Python 原版 mock demo (内置同样的 12 只 mock 股票),
//                打印 v1/v2 放行与拦截明细 + 候选打分排名 + Top3 入场参数 + 铁律提醒
//   --mysql   : 最近交易日全市场真实扫描 (读 wucai_trade: trade_stock_daily /
//                trade_stock_status / trade_sector_daily), 打印 Top K 并落 JSON
//
// 注意: 本程序只对数据库做 SELECT 只读查询

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <mysql.h>
#include <nlohmann/json.hpp>

#include "backtest.hpp"
#include "backtest_data_mysql.hpp"
#include "dragon_picker.hpp"
#include "env.hpp"
#include "mysql_client.hpp"

using json = nlohmann::json;
using quant::dragon::DragonEntryExit;
using quant::dragon::DragonFilterConfig;
using quant::dragon::DragonFilterResult;
using quant::dragon::DragonStockInput;

namespace {

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------

std::string pct(double x) { return fmt::format("{:+.2f}%", x * 100.0); }

void print_usage(const char* prog) {
    fmt::print("用法:\n");
    fmt::print("  {}                 跑内置 12 只 mock 股票 demo (v1/v2 对照 + Top3 入场参数)\n", prog);
    fmt::print("  {} --mysql [选项]   最近交易日全市场真实扫描\n", prog);
    fmt::print("选项:\n");
    fmt::print("  --top K                 打印前 K 名候选 (默认 5)\n");
    fmt::print("  --min-change X          涨幅下限, 小数 (默认 0.05)\n");
    fmt::print("  --max-change X          涨幅上限, 小数 (默认 0.095, 排除近涨停)\n");
    fmt::print("  --max-price X           价格上限, 元 (默认 30)\n");
    fmt::print("  --min-vol-ratio X       量比下限 (默认 2.0)\n");
    fmt::print("  --mcap-low X            流通市值下限, 亿 (默认 30)\n");
    fmt::print("  --mcap-high X           流通市值上限, 亿 (默认 500)\n");
    fmt::print("  --min-listed-days N     上市天数下限, 自然日 (默认 60)\n");
    fmt::print("  --no-sector-resonance   关闭 v2-8 板块共振硬过滤 (v1 对照用)\n");
    fmt::print("  --sector-level L        板块层级 1 或 2 (默认 2; 库中目前只有 level=2)\n");
}

// ---------------------------------------------------------------------------
// 通用打印: 全市场扫描输入表 / 筛选结果 / 入场参数 / 铁律
// ---------------------------------------------------------------------------

void print_input_table(const std::vector<DragonStockInput>& stocks) {
    // 按涨幅降序展示 (与 Python demo 一致)
    std::vector<DragonStockInput> sorted = stocks;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const DragonStockInput& a, const DragonStockInput& b) {
                         return a.day_change_pct > b.day_change_pct;
                     });
    for (const auto& s : sorted) {
        std::string st = quant::dragon::is_st_or_delisting(s.name) ? " (ST!)" : "";
        fmt::print("  {}  {}{}  涨幅 {}  价 {:>7.2f}  量比 {:.1f}  流通市值 {:.0f}亿\n",
                   s.code, s.name, st, pct(s.day_change_pct), s.price, s.volume_ratio,
                   s.float_market_cap / 1e8);
    }
}

void print_filter_rules(const DragonFilterConfig& cfg, bool is_v2) {
    fmt::print("    v1-1: 涨幅 > {}\n", pct(cfg.min_change));
    fmt::print("    v1-2: 涨幅榜前 {}\n", cfg.top_n_scan);
    fmt::print("    v1-3: 流通市值 {:.0f}-{:.0f} 亿\n", cfg.mcap_low / 1e8, cfg.mcap_high / 1e8);
    fmt::print("    v1-4: 量比 > {:.1f}\n", cfg.min_volume_ratio);
    fmt::print("    v1-5: ST / 退市 直接排除\n");
    if (is_v2) {
        fmt::print("    v2-6: 涨幅 < {}   涨停板买不到, T+1 高开污染统计\n", pct(cfg.max_change));
        fmt::print("    v2-7: 上市天数 >= {} 自然日   排除次新股\n", cfg.min_listed_days);
        fmt::print("    v2-8: 板块共振   sector_2 当日涨幅 >= {} 且上涨家数占比 >= {}\n",
                   pct(quant::dragon::SECTOR_MIN_CHANGE_PCT),
                   pct(quant::dragon::SECTOR_MIN_RISE_RATIO));
    } else {
        fmt::print("    (v1 无 v2-6 近涨停 / v2-7 次新股 / v2-8 板块共振 三条补丁)\n");
    }
}

void print_filter_result(const DragonFilterResult& res, const std::string& label) {
    fmt::print("\n  [{}] 放行 {} 只 (按 dragon_score 排序):\n", label, res.candidates.size());
    if (res.candidates.empty()) {
        fmt::print("    (无符合条件的标的)\n");
    }
    for (const auto& c : res.candidates) {
        const double s_chg = c.sector_change_pct.value_or(0.0);
        const double s_rise = c.sector_rise_ratio.value_or(0.0);
        fmt::print("    [{:>2}] {}  {}  涨幅 {}  量比 {:.1f}  市值 {:.0f}亿  价 {:.2f}  "
                   "| 板块 [{}] {} 涨家占比 {:.0f}%  -> dragon_score {:+.3f}\n",
                   c.rank_in_top, c.code, c.name, pct(c.day_change_pct), c.volume_ratio,
                   c.float_market_cap / 1e8, c.price, c.sector_2, pct(s_chg),
                   s_rise * 100.0, c.dragon_score);
    }
    fmt::print("  [{}] 拦截 {} 只:\n", label, res.rejected.size());
    for (const auto& [s, reason] : res.rejected) {
        fmt::print("    [{:>2}] {}  {}  涨幅 {}  -> {}\n", s.rank_in_top, s.code, s.name,
                   pct(s.day_change_pct), reason);
    }
}

void print_entry_params(const std::vector<DragonStockInput>& candidates, size_t top_n) {
    DragonEntryExit entry_calc(1'000'000.0, 0.01, 2.0);
    const size_t n = std::min(top_n, candidates.size());
    for (size_t i = 0; i < n; ++i) {
        const auto e = entry_calc.calc_entry(candidates[i]);
        fmt::print("\n  {}  {}\n", e.code, e.name);
        fmt::print("    入场价: {:.2f}\n", e.entry_price);
        fmt::print("    止损价: {:.2f}  ({:.1f}%)\n", e.stop_loss,
                   (e.stop_loss - e.entry_price) / e.entry_price * 100.0);
        fmt::print("    目标价: {:.2f}  ({:+.1f}%)\n", e.target,
                   (e.target - e.entry_price) / e.entry_price * 100.0);
        fmt::print("    数量:   {} 股\n", e.quantity);
        fmt::print("    金额:   {:.0f} 元\n", e.amount);
        fmt::print("    最大亏损: {:.0f} 元\n", e.max_loss);
        fmt::print("    最大盈利: {:.0f} 元\n", e.max_gain);
        fmt::print("    盈亏比: {:.1f}:1\n", e.payoff_ratio);
        fmt::print("    最长持仓: {} 分钟\n", e.max_hold_minutes);
    }
}

void print_iron_rules() {
    fmt::print("\n{0}\n", std::string(78, '='));
    fmt::print("  铁律提醒\n");
    fmt::print("{0}\n", std::string(78, '='));
    fmt::print("  1. Base Hit 小赢 -- 不追求本垒打, 每股 0.3-0.8 元就走\n");
    fmt::print("  2. 盈亏比 >= 2:1 -- 数学保证: 胜率 33% 也能盈利\n");
    fmt::print("  3. 当日累计亏损 >= 2% -- 强制收手, 不报复性交易\n");
    fmt::print("  4. 连续 3 笔亏损 -- 暂停 1 小时, 让情绪冷静\n");
    fmt::print("  5. 不留隔夜 -- 收盘前 10 分钟全部平仓\n");
    fmt::print("\n");
    fmt::print("  重要警示:\n");
    fmt::print("    Ross Cameron 在 YouTube 公开实盘记录, 用 583 美元做到千万级\n");
    fmt::print("    但 FTC (美国联邦贸易委员会) 起诉指出: 99% 学员复制后亏损\n");
    fmt::print("    这个策略需要极高的纪律性, 不是技术问题, 是心理问题\n");
}

// ---------------------------------------------------------------------------
// JSON 输出
// ---------------------------------------------------------------------------

json config_to_json(const DragonFilterConfig& cfg) {
    return {{"min_change", cfg.min_change},
            {"max_change", cfg.max_change},
            {"max_price", cfg.max_price},
            {"mcap_low", cfg.mcap_low},
            {"mcap_high", cfg.mcap_high},
            {"min_volume_ratio", cfg.min_volume_ratio},
            {"min_listed_days", cfg.min_listed_days},
            {"require_sector_resonance", cfg.require_sector_resonance},
            {"top_n_scan", cfg.top_n_scan}};
}

json stock_to_json(const DragonStockInput& s) {
    json j;
    j["code"] = s.code;
    j["name"] = s.name;
    j["rank_in_top"] = s.rank_in_top;
    j["day_change_pct"] = std::round(s.day_change_pct * 10000.0) / 10000.0;
    j["price"] = s.price;
    j["volume_ratio"] = std::round(s.volume_ratio * 100.0) / 100.0;
    j["float_market_cap"] = s.float_market_cap;
    j["listed_days"] = s.listed_days;
    j["sector_2"] = s.sector_2;
    j["sector_change_pct"] = s.sector_change_pct ? json(*s.sector_change_pct) : json(nullptr);
    j["sector_rise_ratio"] = s.sector_rise_ratio ? json(*s.sector_rise_ratio) : json(nullptr);
    j["dragon_score"] = s.dragon_score;
    return j;
}

json result_to_json(const DragonFilterResult& res) {
    json j;
    json cands = json::array();
    for (const auto& c : res.candidates) cands.push_back(stock_to_json(c));
    j["candidates"] = cands;
    json rejected = json::array();
    for (const auto& [s, reason] : res.rejected) {
        json r = stock_to_json(s);
        r["reject_reason"] = reason;
        rejected.push_back(r);
    }
    j["rejected"] = rejected;
    return j;
}

json entries_to_json(const std::vector<DragonStockInput>& candidates, size_t top_n) {
    DragonEntryExit entry_calc(1'000'000.0, 0.01, 2.0);
    json arr = json::array();
    const size_t n = std::min(top_n, candidates.size());
    for (size_t i = 0; i < n; ++i) {
        const auto e = entry_calc.calc_entry(candidates[i]);
        arr.push_back({{"code", e.code},
                       {"name", e.name},
                       {"entry_price", e.entry_price},
                       {"stop_loss", e.stop_loss},
                       {"target", e.target},
                       {"quantity", e.quantity},
                       {"amount", e.amount},
                       {"max_loss", e.max_loss},
                       {"max_gain", e.max_gain},
                       {"payoff_ratio", e.payoff_ratio},
                       {"max_hold_minutes", e.max_hold_minutes}});
    }
    return arr;
}

void save_json(const std::string& path, const json& j) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        fmt::print("  [警告] JSON 写入失败: {}\n", path);
        return;
    }
    ofs << j.dump(2);
    fmt::print("\n  JSON 已保存: {}\n", path);
}

// ---------------------------------------------------------------------------
// MySQL 模式: 数据加载 (全部只读 SELECT)
// ---------------------------------------------------------------------------

// 股票元信息
struct StatusMeta {
    std::string name;
    std::string sector_1;
    std::string sector_2;
    double float_shares = 0.0;  // 单位: 股
    std::string list_date;      // "YYYY-MM-DD", 可能为空
};

// 板块当日行情
struct SectorRec {
    double change_pct = 0.0;  // 小数 (表里存百分数, 已 /100)
    double rise_ratio = 0.0;  // 上涨家数占比, clip [0,1]
};

std::string query_max_trade_date(quant::mysql::Client& client) {
    if (mysql_query(client.raw(), "SELECT MAX(trade_date) FROM trade_stock_daily") != 0) {
        throw std::runtime_error("查询 MAX(trade_date) 失败: " + client.last_error());
    }
    MYSQL_RES* res = mysql_store_result(client.raw());
    if (!res) throw std::runtime_error("查询 MAX(trade_date) 无结果集");
    MYSQL_ROW row = mysql_fetch_row(res);
    std::string out = (row && row[0]) ? row[0] : "";
    mysql_free_result(res);
    return out;
}

std::unordered_map<std::string, StatusMeta> load_status_meta(quant::mysql::Client& client) {
    std::unordered_map<std::string, StatusMeta> meta;
    const char* sql =
        "SELECT stock_code, stock_name, sector_1, sector_2, float_shares, list_date "
        "FROM trade_stock_status";
    if (mysql_query(client.raw(), sql) != 0) {
        throw std::runtime_error("查询 trade_stock_status 失败: " + client.last_error());
    }
    MYSQL_RES* res = mysql_store_result(client.raw());
    if (!res) throw std::runtime_error("查询 trade_stock_status 无结果集");
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        StatusMeta m;
        m.name = row[1] ? row[1] : "";
        m.sector_1 = row[2] ? row[2] : "";
        m.sector_2 = row[3] ? row[3] : "";
        m.float_shares = (row[4] && row[4][0]) ? std::stod(row[4]) : 0.0;
        m.list_date = row[5] ? row[5] : "";
        meta[row[0] ? row[0] : ""] = std::move(m);
    }
    mysql_free_result(res);
    return meta;
}

// 指定交易日 + sector_level 的板块行情 {sector_name: SectorRec}
std::unordered_map<std::string, SectorRec> load_sector_map(quant::mysql::Client& client,
                                                           const std::string& date,
                                                           int sector_level) {
    std::unordered_map<std::string, SectorRec> out;
    std::ostringstream sql;
    sql << "SELECT sector_name, change_pct, rise_count, stock_count FROM trade_sector_daily "
        << "WHERE trade_date = '" << date << "' AND sector_level = " << sector_level;
    if (mysql_query(client.raw(), sql.str().c_str()) != 0) {
        throw std::runtime_error("查询 trade_sector_daily 失败: " + client.last_error());
    }
    MYSQL_RES* res = mysql_store_result(client.raw());
    if (!res) throw std::runtime_error("查询 trade_sector_daily 无结果集");
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        SectorRec r;
        // change_pct 表里存的是百分数 (2.34 表示 +2.34%), 统一成小数
        r.change_pct = (row[1] && row[1][0]) ? std::stod(row[1]) / 100.0 : 0.0;
        const double rise = (row[2] && row[2][0]) ? std::stod(row[2]) : 0.0;
        const double total = (row[3] && row[3][0]) ? std::stod(row[3]) : 0.0;
        r.rise_ratio = total > 0.0 ? std::min(1.0, std::max(0.0, rise / total)) : 0.0;
        out[row[0] ? row[0] : ""] = r;
    }
    mysql_free_result(res);
    return out;
}

// ---------------------------------------------------------------------------
// MySQL 模式: 最近交易日全市场真实扫描
// ---------------------------------------------------------------------------

int run_mysql_mode(const DragonFilterConfig& cfg, int top_k, int sector_level,
                   const std::string& json_path) {
    fmt::print("\n{0}\n", std::string(78, '='));
    fmt::print("  CASE-22C 龙头战法 -- 最近交易日全市场真实扫描 (MySQL)\n");
    fmt::print("{0}\n", std::string(78, '='));

    auto env = quant::env::find_and_load_dotenv();
    auto mysql_cfg = quant::bt::data::load_mysql_config(env);

    quant::mysql::Client client(mysql_cfg);
    if (!client.connect()) {
        fmt::print("[错误] MySQL 连接失败: {}\n", client.last_error());
        fmt::print("       请检查项目根目录 .env 中的 MYSQL_HOST / MYSQL_PORT / MYSQL_USER / "
                   "MYSQL_PASSWORD / MYSQL_DB (库 wucai_trade)\n");
        return 1;
    }

    // 1) 最近交易日 T, 往回取 40 个自然日窗口 (够 7 个交易日 + 余量)
    const std::string t_max = query_max_trade_date(client);
    if (t_max.empty()) {
        fmt::print("[错误] trade_stock_daily 为空, 请先同步数据\n");
        return 1;
    }
    const std::string start = quant::dragon::shift_date(t_max, -40);
    fmt::print("\n[1] 加载日 K 窗口 {} ~ {} (最近 7 个交易日算量比) ...\n", start, t_max);
    // min_bars=7: T 日 + 至少 6 个前置交易日 (前 6 日均量)
    auto bars_map = quant::bt::data::batch_load_daily(mysql_cfg, start, t_max, 6);
    if (bars_map.empty()) {
        fmt::print("[错误] 窗口内无日 K 数据\n");
        return 1;
    }

    // 2) 全市场最近交易日 T (以数据实际最后一日为准)
    std::string trade_date_t;
    for (const auto& [code, bars] : bars_map) {
        if (!bars.empty()) trade_date_t = std::max(trade_date_t, bars.back().date);
    }
    fmt::print("  共 {} 只股票, 扫描日 T = {}\n", bars_map.size(), trade_date_t);

    fmt::print("[2] 加载股票元信息 trade_stock_status ...\n");
    auto meta = load_status_meta(client);
    fmt::print("  共 {} 只股票元信息\n", meta.size());

    fmt::print("[3] 加载板块行情 trade_sector_daily (T={}, sector_level={}) ...\n",
               trade_date_t, sector_level);
    auto sector_map = load_sector_map(client, trade_date_t, sector_level);
    fmt::print("  共 {} 个板块\n", sector_map.size());
    if (sector_map.empty()) {
        fmt::print("  [警告] 板块数据为空 (可能该 sector_level 无数据), "
                   "v2-8 将淘汰所有股票; 可加 --no-sector-resonance 或 --sector-level 2\n");
    }

    // 3) 拼当日全市场输入
    std::vector<DragonStockInput> stocks;
    for (const auto& [code, bars] : bars_map) {
        if (bars.empty() || bars.back().date != trade_date_t) continue;  // T 日无交易
        const size_t n = bars.size();
        if (n < 2) continue;
        const double close_t = bars[n - 1].close;
        const double prev_close = bars[n - 2].close;  // T-1 收盘
        if (close_t <= 0.0 || prev_close <= 0.0) continue;
        // 量比 = T 量 / 前 6 日均量 (与 04 回测及 Python 原版 6 日窗口口径一致; 不足 6 日用已有天数)
        double vol_sum = 0.0;
        size_t vol_cnt = 0;
        for (size_t i = n - 1; i-- > 0 && vol_cnt < 6;) {
            vol_sum += bars[i].volume;
            ++vol_cnt;
        }
        const double avg_vol = vol_cnt > 0 ? vol_sum / static_cast<double>(vol_cnt) : 0.0;

        const auto it = meta.find(code);
        if (it == meta.end()) continue;  // 无元信息, 与 Python 一致跳过
        const StatusMeta& m = it->second;

        DragonStockInput s;
        s.code = code;
        s.name = m.name;
        s.sector_1 = m.sector_1;
        s.sector_2 = m.sector_2;
        s.day_change_pct = close_t / prev_close - 1.0;
        s.price = close_t;
        s.volume_ratio = avg_vol > 0.0 ? bars[n - 1].volume / avg_vol : 0.0;
        s.float_market_cap = m.float_shares * close_t;
        // listed_days 为自然日; list_date 缺失时 -1 (未知, v2-7 放过)
        s.listed_days = m.list_date.empty()
                            ? -1
                            : quant::dragon::calendar_days_between(m.list_date, trade_date_t);
        // 板块共振字段: 查不到就留空 (v2-8 直接淘汰孤雁)
        if (!m.sector_2.empty()) {
            const auto sit = sector_map.find(m.sector_2);
            if (sit != sector_map.end()) {
                s.sector_change_pct = sit->second.change_pct;
                s.sector_rise_ratio = sit->second.rise_ratio;
            }
        }
        stocks.push_back(std::move(s));
    }
    fmt::print("[4] 拼出当日输入 {} 只, 开始筛选 ...\n", stocks.size());

    // 4) filter + score
    auto res = quant::dragon::filter_dragon_candidates(stocks, cfg);

    fmt::print("\n{0}\n", std::string(78, '='));
    fmt::print("  扫描日 {}  候选 {} 只 (全市场 {} 只, 涨幅榜前 {} 内过滤)\n",
               trade_date_t, res.candidates.size(), stocks.size(), cfg.top_n_scan);
    fmt::print("{0}\n", std::string(78, '='));
    const size_t show = std::min(static_cast<size_t>(top_k), res.candidates.size());
    if (show == 0) {
        fmt::print("  (无符合条件的标的)\n");
    }
    for (size_t i = 0; i < show; ++i) {
        const auto& c = res.candidates[i];
        const double s_chg = c.sector_change_pct.value_or(0.0);
        const double s_rise = c.sector_rise_ratio.value_or(0.0);
        fmt::print("  Top{} [{:>2}] {}  {}  涨幅 {}  量比 {:.1f}  市值 {:.0f}亿  价 {:.2f}\n",
                   i + 1, c.rank_in_top, c.code, c.name, pct(c.day_change_pct),
                   c.volume_ratio, c.float_market_cap / 1e8, c.price);
        fmt::print("         板块 [{}] {} 涨家占比 {:.0f}%  上市 {} 天  -> dragon_score {:+.3f}\n",
                   c.sector_2, pct(s_chg), s_rise * 100.0, c.listed_days, c.dragon_score);
    }

    // 5) Top K 入场参数
    if (show > 0) {
        fmt::print("\n[5] Top {} 入场参数 (Base Hit 小赢 + 2:1 盈亏比 + 1% 单笔风险)\n", show);
        print_entry_params(res.candidates, show);
    }
    print_iron_rules();

    // 6) 落 JSON
    json j;
    j["mode"] = "mysql";
    j["scan_date"] = trade_date_t;
    j["universe_size"] = stocks.size();
    j["config"] = config_to_json(cfg);
    j["sector_level"] = sector_level;
    j["candidates"] = json::array();
    for (const auto& c : res.candidates) j["candidates"].push_back(stock_to_json(c));
    j["top_entries"] = entries_to_json(res.candidates, show);
    save_json(json_path, j);

    fmt::print("\n[结果] {{\"status\": \"success\", \"scan_date\": \"{}\", \"candidates\": {}}}\n",
               trade_date_t, res.candidates.size());
    return 0;
}

// ---------------------------------------------------------------------------
// 默认模式: Python 原版 mock demo (12 只内置股票)
// ---------------------------------------------------------------------------

int run_mock_demo(const std::string& json_path) {
    fmt::print("\n{0}\n", std::string(78, '='));
    fmt::print("  CASE-22C 龙头战法 demo -- A 股化首板战法\n");
    fmt::print("{0}\n", std::string(78, '='));

    // 1) 拉今日候选 (mock)
    fmt::print("\n[1] 模拟当日全市场涨跌数据 (12 只代表性标的)\n");
    const auto today_stocks = quant::dragon::build_mock_today_stocks();
    print_input_table(today_stocks);

    // 2) 筛选法则说明
    fmt::print("\n[2] 应用筛选法则 (v1 5 法则 + v2 3 条补丁):\n");
    print_filter_rules(DragonFilterConfig{}, true);

    // 3) v2: 完整硬规则 (板块共振开)
    DragonFilterConfig cfg_v2;  // 全部默认
    auto res_v2 = quant::dragon::filter_dragon_candidates(today_stocks, cfg_v2);
    print_filter_result(res_v2, "v2 完整硬规则");

    // 4) v1 对照: 无板块共振 / 次新股 / 近涨停过滤
    DragonFilterConfig cfg_v1;
    cfg_v1.max_change = 999.0;                 // 关闭 v2-6
    cfg_v1.min_listed_days = 0;                // 关闭 v2-7
    cfg_v1.require_sector_resonance = false;   // 关闭 v2-8
    auto res_v1 = quant::dragon::filter_dragon_candidates(today_stocks, cfg_v1);
    fmt::print("\n  (v1 对照口径: 无 v2-6 近涨停 / v2-7 次新股 / v2-8 板块共振 过滤)\n");
    print_filter_result(res_v1, "v1 对照");

    if (res_v2.candidates.empty()) {
        fmt::print("\n  (v2 无符合条件的标的, 跳过分入场参数)\n");
    } else {
        // 5) v2 Top 3 入场参数
        fmt::print("\n[3] v2 Top 3 入场参数 (Base Hit 小赢 + 2:1 盈亏比 + 1% 单笔风险)\n");
        print_entry_params(res_v2.candidates, 3);
    }

    print_iron_rules();

    // 6) 落 JSON
    json j;
    j["mode"] = "mock";
    j["config_v2"] = config_to_json(cfg_v2);
    j["config_v1"] = config_to_json(cfg_v1);
    j["v2"] = result_to_json(res_v2);
    j["v1"] = result_to_json(res_v1);
    j["top3_entries_v2"] = entries_to_json(res_v2.candidates, 3);
    save_json(json_path, j);

    fmt::print("\n[结果] {{\"status\": \"success\", \"v2_candidates\": {}, \"v1_candidates\": {}}}\n",
               res_v2.candidates.size(), res_v1.candidates.size());
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    // CLI 参数 (默认值与 Python 一致; mcap 用亿为单位方便输入)
    DragonFilterConfig cfg;
    int top_k = 5;
    int sector_level = 2;
    bool use_mysql = false;
    double mcap_low_yi = 30.0;
    double mcap_high_yi = 500.0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mysql") {
            use_mysql = true;
        } else if ((arg == "--help" || arg == "-h")) {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--top" && i + 1 < argc) {
            top_k = std::stoi(argv[++i]);
        } else if (arg == "--min-change" && i + 1 < argc) {
            cfg.min_change = std::stod(argv[++i]);
        } else if (arg == "--max-change" && i + 1 < argc) {
            cfg.max_change = std::stod(argv[++i]);
        } else if (arg == "--max-price" && i + 1 < argc) {
            cfg.max_price = std::stod(argv[++i]);
        } else if (arg == "--min-vol-ratio" && i + 1 < argc) {
            cfg.min_volume_ratio = std::stod(argv[++i]);
        } else if (arg == "--mcap-low" && i + 1 < argc) {
            mcap_low_yi = std::stod(argv[++i]);
        } else if (arg == "--mcap-high" && i + 1 < argc) {
            mcap_high_yi = std::stod(argv[++i]);
        } else if (arg == "--min-listed-days" && i + 1 < argc) {
            cfg.min_listed_days = std::stoi(argv[++i]);
        } else if (arg == "--no-sector-resonance") {
            cfg.require_sector_resonance = false;
        } else if (arg == "--sector-level" && i + 1 < argc) {
            sector_level = std::stoi(argv[++i]);
        } else {
            fmt::print("[警告] 忽略未知参数: {}\n", arg);
        }
    }
    cfg.mcap_low = mcap_low_yi * 1e8;
    cfg.mcap_high = mcap_high_yi * 1e8;

    std::filesystem::create_directories("outputs/week11");
    const std::string json_path = "outputs/week11/dragon_picker.json";

    try {
        if (use_mysql) {
            return run_mysql_mode(cfg, top_k, sector_level, json_path);
        }
        (void)sector_level;  // demo 模式不用
        return run_mock_demo(json_path);
    } catch (const std::exception& e) {
        fmt::print("\n[错误] 运行异常: {}\n", e.what());
        fmt::print("       若与数据库相关, 请检查项目根目录 .env 中的 MYSQL_* 配置 "
                   "(库 wucai_trade), 以及 MySQL 服务是否可用\n");
        return 1;
    }
}
