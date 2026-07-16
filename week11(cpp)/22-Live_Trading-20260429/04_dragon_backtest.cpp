// 22-实盘作战与CEO控制台 / CASE-龙头战法/dragon_strategy/dragon_backtest.py 的 C++ 实现
// 龙头战法全市场 T+1 历史回测 (读 wucai_trade.*)
//
// 口径设计 (A 股 T+1, 不能日内平仓):
//   每个交易日 T:
//     1. 拼全市场候选 (信号口径与 03_dragon_picker 完全一致):
//          day_change_pct   = close_T / close_(T-1) - 1
//          volume_ratio     = volume_T / mean(volume_(T-6..T-1))   (前 6 日均量, 与 Python 一致)
//          price            = close_T
//          float_market_cap = float_shares * close_T               (trade_stock_status)
//          listed_days      = T - list_date (自然日)
//          板块共振         = trade_sector_daily (sector_2 当日涨幅 / 上涨家数占比)
//     2. filter_dragon_candidates + calc_dragon_score, 每日取 Top K
//     3. T+1 开盘价买, T+H 收盘价卖, ret = sell_close / buy_open - 1
//
//   【毛收益口径】无手续费 / 滑点 / 印花税 / 仓位管理, 无涨跌停成交模拟 (与 Python 原版一致)
//
// 性能: 全市场 7000+ 股 x 250+ 交易日逐日扫描, 所有数据一次性 load 进内存
//       (batch_load_daily + status 全表 + sector 全表), 循环内只做内存查询, 不发 SQL
//
// 注意: 本程序只对数据库做 SELECT 只读查询

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <matplot/matplot.h>
#include <mysql.h>

#include "backtest.hpp"
#include "backtest_data_mysql.hpp"
#include "dragon_picker.hpp"
#include "env.hpp"
#include "mysql_client.hpp"

using quant::dragon::DragonFilterConfig;
using quant::dragon::DragonStockInput;

namespace {

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------

std::string pct(double x) { return fmt::format("{:+.2f}%", x * 100.0); }

// "20250101" / "2025-01-01" -> "2025-01-01" (非法输入原样返回)
std::string normalize_date(const std::string& s) {
    std::string ds;
    for (char c : s) {
        if (c >= '0' && c <= '9') ds.push_back(c);
    }
    if (ds.size() != 8) return s;
    return ds.substr(0, 4) + "-" + ds.substr(4, 2) + "-" + ds.substr(6, 2);
}

// 与 Python round(x, n) 对齐的四舍五入; 消除 -0.0 显示
double round_n(double x, double scale) {
    const double v = std::round(x * scale) / scale;
    return v == 0.0 ? 0.0 : v;
}
double round4(double x) { return round_n(x, 10000.0); }
double round3(double x) { return round_n(x, 1000.0); }
double round2(double x) { return round_n(x, 100.0); }

void print_usage(const char* prog) {
    fmt::print("用法: {} [选项]\n", prog);
    fmt::print("  --start YYYYMMDD          回测起始日 (默认 2025-01-01)\n");
    fmt::print("  --end YYYYMMDD            回测结束日 (默认 2026-04-01)\n");
    fmt::print("  --top K                   每日 Top K 候选 (默认 5)\n");
    fmt::print("  --hold 1,3,5              持有天数列表, 逗号分隔 (默认 1,3,5)\n");
    fmt::print("  --min-change X            涨幅下限, 小数 (默认 0.05)\n");
    fmt::print("  --max-change X            涨幅上限, 小数 (默认 0.095, 排除近涨停)\n");
    fmt::print("  --max-price X             价格上限, 元 (默认 30)\n");
    fmt::print("  --min-vol-ratio X         量比下限 (默认 2.0)\n");
    fmt::print("  --mcap-low X              流通市值下限, 亿 (默认 30)\n");
    fmt::print("  --mcap-high X             流通市值上限, 亿 (默认 500)\n");
    fmt::print("  --min-listed-days N       上市天数下限, 自然日 (默认 60)\n");
    fmt::print("  --no-sector-resonance     关闭 v2-8 板块共振硬过滤 (v1 对照用)\n");
    fmt::print("  --sector-level L          板块层级 1 或 2 (默认 2; 库中目前只有 level=2)\n");
}

// ---------------------------------------------------------------------------
// 数据结构
// ---------------------------------------------------------------------------

// 股票元信息 (trade_stock_status)
struct StatusMeta {
    std::string name;
    std::string sector_1;
    std::string sector_2;
    double float_shares = 0.0;  // 单位: 股
    std::string list_date;      // "YYYY-MM-DD", 可能为空
};

// 板块当日行情 (trade_sector_daily)
struct SectorRec {
    double change_pct = 0.0;  // 小数 (表里存百分数, 已 /100)
    double rise_ratio = 0.0;  // 上涨家数占比, clip [0,1]
};

// 单只股票的日 K 序列 + 按全市场交易日对齐的快速索引
struct StockSeries {
    std::vector<quant::bt::Bar> bars;  // 按日期升序
    std::vector<int> pos;              // pos[di] = bars 下标; 当日无数据为 -1
    std::vector<int> prev_count;       // prev_count[di] = 日期 < dates[di] 的 bar 数
};

// 一笔模拟交易 (对应 Python simulate_trades 一行)
struct DragonTrade {
    std::string signal_date;
    std::string buy_date;
    std::string sell_date;
    std::string code;
    std::string name;
    std::string sector_2;
    std::string sector_1;
    double buy_open = 0.0;
    double sell_close = 0.0;
    double ret = 0.0;
    double score = 0.0;
    double day_change = 0.0;
    double vol_ratio = 0.0;
    double sector_chg = 0.0;
    double sector_rise = 0.0;
};

// 净值曲线一个点
struct CurvePoint {
    std::string buy_date;
    double daily_ret = 0.0;
    double nav = 0.0;
};

// ---------------------------------------------------------------------------
// MySQL 数据加载 (全部只读 SELECT, 一次性拉进内存)
// ---------------------------------------------------------------------------

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

// 区间内板块全量: key = "sector_name|trade_date"
std::unordered_map<std::string, SectorRec> load_sector_panel(quant::mysql::Client& client,
                                                             const std::string& start,
                                                             const std::string& end,
                                                             int sector_level) {
    std::unordered_map<std::string, SectorRec> out;
    std::ostringstream sql;
    sql << "SELECT sector_name, trade_date, change_pct, rise_count, stock_count "
        << "FROM trade_sector_daily "
        << "WHERE trade_date >= '" << start << "' AND trade_date <= '" << end
        << "' AND sector_level = " << sector_level;
    if (mysql_query(client.raw(), sql.str().c_str()) != 0) {
        throw std::runtime_error("查询 trade_sector_daily 失败: " + client.last_error());
    }
    MYSQL_RES* res = mysql_store_result(client.raw());
    if (!res) throw std::runtime_error("查询 trade_sector_daily 无结果集");
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        SectorRec r;
        // change_pct 表里存的是百分数 (例 2.34 表示 +2.34%), 统一成小数
        r.change_pct = (row[2] && row[2][0]) ? std::stod(row[2]) / 100.0 : 0.0;
        const double rise = (row[3] && row[3][0]) ? std::stod(row[3]) : 0.0;
        const double total = (row[4] && row[4][0]) ? std::stod(row[4]) : 0.0;
        r.rise_ratio = total > 0.0 ? std::min(1.0, std::max(0.0, rise / total)) : 0.0;
        std::string key = std::string(row[0] ? row[0] : "") + "|" + (row[1] ? row[1] : "");
        out[std::move(key)] = r;
    }
    mysql_free_result(res);
    return out;
}

// ---------------------------------------------------------------------------
// 统计
// ---------------------------------------------------------------------------

struct Summary {
    int n = 0;
    double win_rate = 0.0;
    double avg_ret = 0.0;
    double median = 0.0;
    double best = 0.0;
    double worst = 0.0;
};

Summary summarize(const std::vector<DragonTrade>& trades) {
    Summary s;
    if (trades.empty()) return s;
    s.n = static_cast<int>(trades.size());
    std::vector<double> rets;
    rets.reserve(trades.size());
    int wins = 0;
    double sum = 0.0;
    for (const auto& t : trades) {
        rets.push_back(t.ret);
        sum += t.ret;
        if (t.ret > 0.0) ++wins;
    }
    std::sort(rets.begin(), rets.end());
    s.win_rate = static_cast<double>(wins) / s.n;
    s.avg_ret = sum / s.n;
    const size_t n = rets.size();
    s.median = (n % 2 == 1) ? rets[n / 2] : (rets[n / 2 - 1] + rets[n / 2]) / 2.0;
    s.best = rets.back();
    s.worst = rets.front();
    return s;
}

// 按 buy_date 等权聚合: 当天若有 K 笔取均值; 按交易日累乘成净值
std::vector<CurvePoint> equity_curve(const std::vector<DragonTrade>& trades) {
    std::map<std::string, std::pair<double, int>> daily;  // buy_date -> (sum_ret, cnt)
    for (const auto& t : trades) {
        auto& acc = daily[t.buy_date];
        acc.first += t.ret;
        acc.second += 1;
    }
    std::vector<CurvePoint> curve;
    curve.reserve(daily.size());
    double nav = 1.0;
    for (const auto& [date, acc] : daily) {
        const double r = acc.first / acc.second;
        nav *= (1.0 + r);
        curve.push_back({date, r, nav});
    }
    return curve;
}

struct PerfMetrics {
    int trade_days = 0;
    double cum_return = 0.0;
    double annualized = 0.0;
    double sharpe = 0.0;
    double max_drawdown = 0.0;
};

PerfMetrics perf_metrics(const std::vector<CurvePoint>& curve) {
    PerfMetrics m;
    if (curve.empty()) return m;
    const int days = static_cast<int>(curve.size());
    m.trade_days = days;
    m.cum_return = curve.back().nav - 1.0;
    // 年化 (252)
    const double ann_factor = 252.0 / std::max(days, 1);
    m.annualized = curve.back().nav > 0.0
                       ? std::pow(1.0 + m.cum_return, ann_factor) - 1.0
                       : -1.0;
    // Sharpe (rf=0, 日收益总体 std, x sqrt(252))
    if (days > 1) {
        double mean = 0.0;
        for (const auto& p : curve) mean += p.daily_ret;
        mean /= days;
        double var = 0.0;
        for (const auto& p : curve) var += (p.daily_ret - mean) * (p.daily_ret - mean);
        const double std_dev = std::sqrt(var / days);  // numpy .std() 默认 ddof=0
        m.sharpe = mean / (std_dev + 1e-9) * std::sqrt(252.0);
    }
    // 最大回撤
    double peak = curve.front().nav;
    for (const auto& p : curve) {
        peak = std::max(peak, p.nav);
        m.max_drawdown = std::min(m.max_drawdown, p.nav / peak - 1.0);
    }
    return m;
}

struct SectorAgg {
    std::string sector;
    int n = 0;
    double win_rate = 0.0;
    double avg_ret = 0.0;
};

std::vector<SectorAgg> by_sector(const std::vector<DragonTrade>& trades, int sector_level,
                                 size_t top_n) {
    const bool use_l2 = (sector_level == 2);
    std::map<std::string, std::pair<int, std::pair<int, double>>> agg;  // sector -> (n, (wins, sum))
    for (const auto& t : trades) {
        const std::string& sec = use_l2 ? t.sector_2 : t.sector_1;
        auto& a = agg[sec];
        a.first += 1;
        if (t.ret > 0.0) a.second.first += 1;
        a.second.second += t.ret;
    }
    std::vector<SectorAgg> out;
    out.reserve(agg.size());
    for (const auto& [sec, a] : agg) {
        SectorAgg sa;
        sa.sector = sec;
        sa.n = a.first;
        sa.win_rate = static_cast<double>(a.second.first) / a.first;
        sa.avg_ret = a.second.second / a.first;
        out.push_back(sa);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const SectorAgg& a, const SectorAgg& b) { return a.n > b.n; });
    if (out.size() > top_n) out.resize(top_n);
    return out;
}

// ---------------------------------------------------------------------------
// CSV 落盘 (带 UTF-8 BOM, Excel 直接打开不乱码)
// ---------------------------------------------------------------------------

void write_trades_csv(const std::string& path, const std::vector<DragonTrade>& trades) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return;
    ofs << "\xEF\xBB\xBF";  // UTF-8 BOM
    ofs << "signal_date,buy_date,sell_date,code,name,sector_2,sector_1,"
           "buy_open,sell_close,ret,score,day_change,vol_ratio,sector_chg,sector_rise\n";
    for (const auto& t : trades) {
        ofs << fmt::format("{},{},{},{},{},{},{},{:.4f},{:.4f},{:.4f},{:.3f},{:.4f},{:.2f},"
                           "{:.4f},{:.3f}\n",
                           t.signal_date, t.buy_date, t.sell_date, t.code, t.name, t.sector_2,
                           t.sector_1, t.buy_open, t.sell_close, t.ret, t.score, t.day_change,
                           t.vol_ratio, t.sector_chg, t.sector_rise);
    }
}

void write_curve_csv(const std::string& path, const std::vector<CurvePoint>& curve) {
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return;
    ofs << "\xEF\xBB\xBF";  // UTF-8 BOM
    ofs << "buy_date,daily_ret,nav\n";
    for (const auto& p : curve) {
        ofs << fmt::format("{},{:.6f},{:.6f}\n", p.buy_date, p.daily_ret, p.nav);
    }
}

// ---------------------------------------------------------------------------
// Matplot++ 净值对比图 (每个 H 一条曲线)
// ---------------------------------------------------------------------------

void save_nav_chart(const std::vector<std::pair<int, std::vector<CurvePoint>>>& curves,
                    const std::string& path) {
    namespace mp = matplot;
    auto fig = mp::figure(false);
    fig->size(1400, 800);
    auto ax = mp::gca();
    ax->hold(mp::on);

    // 找最长曲线做 x 轴日期标签
    const std::vector<CurvePoint>* longest = nullptr;
    for (const auto& [h, curve] : curves) {
        if (curve.empty()) continue;
        std::vector<double> xs(curve.size()), ys(curve.size());
        for (size_t i = 0; i < curve.size(); ++i) {
            xs[i] = static_cast<double>(i);
            ys[i] = curve[i].nav;
        }
        ax->plot(xs, ys, "-")->display_name(fmt::format("H={}", h));
        if (!longest || curve.size() > longest->size()) longest = &curve;
    }
    if (longest) {
        const size_t n = longest->size();
        const size_t step = std::max<size_t>(1, n / 8);
        std::vector<double> tick_xs;
        std::vector<std::string> tick_labels;
        for (size_t i = 0; i < n; i += step) {
            tick_xs.push_back(static_cast<double>(i));
            tick_labels.push_back((*longest)[i].buy_date);
        }
        ax->xticks(tick_xs);
        ax->xticklabels(tick_labels);
    }
    ax->title("龙头战法净值对比 (T+1 开盘买, T+H 收盘卖, 毛收益口径)");
    ax->xlabel("买入日");
    ax->ylabel("净值");
    ax->legend();
    ax->grid(mp::on);

    fig->save(path);
    fmt::print("  净值对比图已保存: {}\n", path);
}

} // namespace

int main(int argc, char* argv[]) {
    // CLI 参数 (--start/--end 给合理默认值, 方便无参直接跑; mcap 用亿为单位)
    std::string start = "2025-01-01";
    std::string end = "2026-04-01";
    int top_k = 5;
    std::vector<int> holds = {1, 3, 5};
    int sector_level = 2;
    DragonFilterConfig cfg;
    double mcap_low_yi = 30.0;
    double mcap_high_yi = 500.0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--start" && i + 1 < argc) {
            start = argv[++i];
        } else if (arg == "--end" && i + 1 < argc) {
            end = argv[++i];
        } else if (arg == "--top" && i + 1 < argc) {
            top_k = std::stoi(argv[++i]);
        } else if (arg == "--hold" && i + 1 < argc) {
            holds.clear();
            std::stringstream ss(argv[++i]);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                if (!tok.empty()) holds.push_back(std::stoi(tok));
            }
            if (holds.empty()) holds = {1, 3, 5};
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
    start = normalize_date(start);
    end = normalize_date(end);
    cfg.mcap_low = mcap_low_yi * 1e8;
    cfg.mcap_high = mcap_high_yi * 1e8;

    std::filesystem::create_directories("outputs/week11");
    const std::string out_dir = "outputs/week11";

    try {
        // ---- 头部横幅 ----
        std::string holds_str;
        for (size_t i = 0; i < holds.size(); ++i) {
            if (i > 0) holds_str += ",";
            holds_str += std::to_string(holds[i]);
        }
        fmt::print("\n{0}\n", std::string(70, '#'));
        fmt::print("# CASE-22C 龙头战法回测  {} ~ {}\n", start, end);
        fmt::print("# Top {} / hold {} / min_change {:.2f} / max_change {:.3f}\n", top_k,
                   holds_str, cfg.min_change, cfg.max_change);
        fmt::print("# max_price {:.0f} / vol_ratio>={:.1f} / mcap [{:.0f}-{:.0f}]亿\n",
                   cfg.max_price, cfg.min_volume_ratio, cfg.mcap_low / 1e8, cfg.mcap_high / 1e8);
        fmt::print("# 上市天数>={}  板块共振={} (sector_chg>={:.1f}%, rise_ratio>={:.0f}%)\n",
                   cfg.min_listed_days, cfg.require_sector_resonance ? "开" : "关",
                   quant::dragon::SECTOR_MIN_CHANGE_PCT * 100.0,
                   quant::dragon::SECTOR_MIN_RISE_RATIO * 100.0);
        fmt::print("# 毛收益口径: 无手续费/滑点/印花税/仓位管理, 无涨跌停成交模拟\n");
        fmt::print("{0}\n", std::string(70, '#'));

        // ---- 连接数据库 ----
        auto env = quant::env::find_and_load_dotenv();
        auto mysql_cfg = quant::bt::data::load_mysql_config(env);
        quant::mysql::Client client(mysql_cfg);
        if (!client.connect()) {
            fmt::print("[错误] MySQL 连接失败: {}\n", client.last_error());
            fmt::print("       请检查项目根目录 .env 中的 MYSQL_HOST / MYSQL_PORT / MYSQL_USER / "
                       "MYSQL_PASSWORD / MYSQL_DB (库 wucai_trade)\n");
            return 1;
        }

        // ---- [1] 市场元信息 ----
        fmt::print("\n[1] 读市场元信息 (股票名/板块/流通股本/上市日期) ...\n");
        auto meta = load_status_meta(client);
        if (meta.empty()) {
            fmt::print("  [ERROR] trade_stock_status 为空, 请先跑 21-CASE-A 同步数据\n");
            return 1;
        }
        fmt::print("  共 {} 只股票\n", meta.size());

        // ---- [2] 日 K 面板 ----
        fmt::print("\n[2] 读日 K 面板 {} ~ {} ...\n", start, end);
        // min_bars=1: 不按区间长度过滤, 预热 (T 前 >= 6 个交易日) 在逐日循环里按股判断
        auto bars_map = quant::bt::data::batch_load_daily(mysql_cfg, start, end, 1);
        if (bars_map.empty()) {
            fmt::print("  [ERROR] trade_stock_daily 区间为空, 请确认 21-CASE-A 数据范围\n");
            return 1;
        }
        size_t total_rows = 0;
        for (const auto& [code, bars] : bars_map) total_rows += bars.size();
        fmt::print("  共 {} 只股票 / {} 行\n", bars_map.size(), total_rows);

        // ---- [2.1] 板块面板 ----
        fmt::print("\n[2.1] 读板块面板 trade_sector_daily (sector_level={}) ...\n", sector_level);
        auto sector_panel = load_sector_panel(client, start, end, sector_level);
        if (sector_panel.empty()) {
            fmt::print("  [ERROR] trade_sector_daily 区间为空 (sector_level={}), "
                       "请先跑板块行情采集或换 --sector-level 2\n", sector_level);
            return 1;
        }
        fmt::print("  共 {} 行\n", sector_panel.size());

        // ---- 全市场交易日 + 每股对齐索引 ----
        std::set<std::string> all_dates;
        for (const auto& [code, bars] : bars_map) {
            for (const auto& b : bars) all_dates.insert(b.date);
        }
        std::vector<std::string> dates(all_dates.begin(), all_dates.end());
        const int n_dates = static_cast<int>(dates.size());
        fmt::print("  交易日: {} 天 ({} -> {})\n", n_dates, dates.front(), dates.back());

        std::unordered_map<std::string, int> date_idx;
        date_idx.reserve(dates.size() * 2);
        for (int i = 0; i < n_dates; ++i) date_idx[dates[i]] = i;

        std::unordered_map<std::string, StockSeries> series;
        series.reserve(bars_map.size());
        for (auto& [code, bars] : bars_map) {
            StockSeries ss;
            ss.bars = std::move(bars);
            ss.pos.assign(n_dates, -1);
            ss.prev_count.assign(n_dates, 0);
            for (int j = 0; j < static_cast<int>(ss.bars.size()); ++j) {
                const auto it = date_idx.find(ss.bars[j].date);
                if (it != date_idx.end()) ss.pos[it->second] = j;
            }
            int cnt = 0;
            for (int di = 0; di < n_dates; ++di) {
                ss.prev_count[di] = cnt;         // 日期 < dates[di] 的 bar 数
                if (ss.pos[di] >= 0) ++cnt;
            }
            series[code] = std::move(ss);
        }
        bars_map.clear();

        // ---- [3] 逐日生成龙头候选 ----
        fmt::print("\n[3] 逐日生成龙头候选 ...\n");
        // key: 全局交易日下标 -> 当日 Top K 候选
        std::map<int, std::vector<DragonStockInput>> picks_per_day;
        size_t pick_count = 0;
        for (int i = 0; i < n_dates; ++i) {
            // 至少要有 6 个交易日的历史窗口 (与 Python i<6 跳过一致)
            if (i < 6) continue;
            const std::string& t = dates[i];
            std::vector<DragonStockInput> raw;
            for (const auto& [code, ss] : series) {
                const int j = ss.pos[i];
                if (j < 0) continue;             // T 日无交易 (停牌等)
                const int p = ss.prev_count[i];
                if (p < 6) continue;             // 预热: T 前需 >= 6 个交易日数据
                const quant::bt::Bar& bar_t = ss.bars[j];
                const double close_t = bar_t.close;
                if (close_t <= 0.0) continue;
                const double pc = ss.bars[p - 1].close;  // T-1 收盘
                if (pc <= 0.0) continue;
                // 均量口径与 Python 一致: 取 T 前 6 个市场交易日窗口内该股实际成交量的均值
                // (Python: window=panel[trade_date<t] 的最后 6 个 trade_date, groupby(stock).volume.mean())
                double vol_sum = 0.0;
                int vol_cnt = 0;
                for (int d = i - 6; d <= i - 1; ++d) {
                    const int bj = ss.pos[d];
                    if (bj >= 0) { vol_sum += ss.bars[bj].volume; ++vol_cnt; }
                }
                const double avg_vol = vol_cnt > 0 ? vol_sum / vol_cnt : 0.0;

                const auto mit = meta.find(code);
                if (mit == meta.end()) continue;  // 无元信息, 与 Python 一致跳过
                const StatusMeta& m = mit->second;

                DragonStockInput s;
                s.code = code;
                s.name = m.name;
                s.sector_1 = m.sector_1;
                s.sector_2 = m.sector_2;
                s.day_change_pct = close_t / pc - 1.0;
                s.price = close_t;
                s.volume_ratio = avg_vol > 0.0 ? bar_t.volume / avg_vol : 0.0;
                s.float_market_cap = m.float_shares * close_t;
                s.listed_days = m.list_date.empty()
                                    ? -1
                                    : quant::dragon::calendar_days_between(m.list_date, t);
                // 板块共振字段 (sector_2 当日表现); 查不到留空 -> v2-8 淘汰
                if (!m.sector_2.empty()) {
                    const auto sit = sector_panel.find(m.sector_2 + "|" + t);
                    if (sit != sector_panel.end()) {
                        s.sector_change_pct = sit->second.change_pct;
                        s.sector_rise_ratio = sit->second.rise_ratio;
                    }
                }
                raw.push_back(std::move(s));
            }
            if (raw.empty()) continue;

            // 全部硬规则 + 打分交给 dragon_picker, 保证 picker 与 backtest 口径一致
            auto res = quant::dragon::filter_dragon_candidates(std::move(raw), cfg);
            if (!res.candidates.empty()) {
                const size_t k = std::min(static_cast<size_t>(top_k), res.candidates.size());
                auto& picks = picks_per_day[i];
                picks.assign(res.candidates.begin(), res.candidates.begin() + k);
                pick_count += picks.size();
            }
            if ((i + 1) % 50 == 0 || i + 1 == n_dates) {
                fmt::print("  进度 {}/{} 天, 已生成 {} 笔信号\n", i + 1, n_dates, pick_count);
            }
        }
        fmt::print("  生成候选 {} 天, 共 {} 笔信号\n", picks_per_day.size(), pick_count);

        if (picks_per_day.empty()) {
            fmt::print("  [INFO] 该参数下无任何候选, 终止\n");
            return 0;
        }

        // ---- [4] 逐持有期 H 模拟 + 统计 ----
        std::vector<std::pair<int, std::vector<CurvePoint>>> curves_for_plot;
        for (const int hold : holds) {
            fmt::print("\n{0}\n", std::string(70, '='));
            fmt::print("  持有 H = {} 日 (T+1.open 买, T+{}.close 卖)\n", hold, hold);
            fmt::print("{0}\n", std::string(70, '='));

            // 模拟交易: T+1.open 买, T+H.close 卖
            std::vector<DragonTrade> trades;
            for (const auto& [i, picks] : picks_per_day) {
                if (i + hold >= n_dates) continue;  // 区间尾部不足 H 日的信号丢弃
                for (const auto& c : picks) {
                    const auto sit = series.find(c.code);
                    if (sit == series.end()) continue;
                    const StockSeries& ss = sit->second;
                    const int jb = ss.pos[i + 1];
                    const int js = ss.pos[i + hold];
                    if (jb < 0 || js < 0) continue;  // 买/卖日无 K 线 (停牌等), 与 Python KeyError 一致
                    const double buy_open = ss.bars[jb].open;
                    const double sell_close = ss.bars[js].close;
                    if (buy_open <= 0.0) continue;

                    DragonTrade tr;
                    tr.signal_date = dates[i];
                    tr.buy_date = dates[i + 1];
                    tr.sell_date = dates[i + hold];
                    tr.code = c.code;
                    tr.name = c.name;
                    tr.sector_2 = c.sector_2;
                    tr.sector_1 = c.sector_1;
                    tr.buy_open = round4(buy_open);
                    tr.sell_close = round4(sell_close);
                    tr.ret = round4(sell_close / buy_open - 1.0);
                    tr.score = round3(c.dragon_score);
                    tr.day_change = round4(c.day_change_pct);
                    tr.vol_ratio = round2(c.volume_ratio);
                    // v2 字段, 复盘时看共振有没有起到作用
                    tr.sector_chg = round4(c.sector_change_pct.value_or(0.0));
                    tr.sector_rise = round3(c.sector_rise_ratio.value_or(0.0));
                    trades.push_back(std::move(tr));
                }
            }
            if (trades.empty()) {
                fmt::print("  无成交\n");
                continue;
            }

            const Summary s = summarize(trades);
            fmt::print("  样本: {} 笔   胜率 {:.2f}%   均收 {}   中位 {}   最优 {}   最差 {}\n",
                       s.n, s.win_rate * 100.0, pct(s.avg_ret), pct(s.median), pct(s.best),
                       pct(s.worst));

            const auto curve = equity_curve(trades);
            const PerfMetrics m = perf_metrics(curve);
            fmt::print("  净值曲线: {} 个买入日   累计 {}   年化 {}   Sharpe {:.3f}   MDD {}\n",
                       m.trade_days, pct(m.cum_return), pct(m.annualized), m.sharpe,
                       pct(m.max_drawdown));

            const auto sec = by_sector(trades, sector_level, 10);
            if (!sec.empty()) {
                fmt::print("\n  按 sector_{} 汇总 (前 10):\n", sector_level == 2 ? 2 : 1);
                for (const auto& sa : sec) {
                    fmt::print("    {}  n={:>3}  胜率 {:.2f}%  均收 {}\n", sa.sector, sa.n,
                               sa.win_rate * 100.0, pct(sa.avg_ret));
                }
            }

            // 落盘 CSV
            const std::string trades_path = out_dir + "/dragon_trades_H" + std::to_string(hold) + ".csv";
            const std::string curve_path = out_dir + "/dragon_curve_H" + std::to_string(hold) + ".csv";
            write_trades_csv(trades_path, trades);
            write_curve_csv(curve_path, curve);
            fmt::print("\n  CSV: {}, {} -> {}\n", trades_path, curve_path, out_dir);

            curves_for_plot.emplace_back(hold, curve);
        }

        // ---- [5] 净值对比图 ----
        bool any_curve = false;
        for (const auto& [h, curve] : curves_for_plot) {
            if (!curve.empty()) any_curve = true;
        }
        if (any_curve) {
            save_nav_chart(curves_for_plot, out_dir + "/dragon_nav.png");
        }

        fmt::print("\n[结果] {{\"status\": \"success\", \"signal_count\": {}, \"holds\": \"{}\"}}\n",
                   pick_count, holds_str);
        return 0;
    } catch (const std::exception& e) {
        fmt::print("\n[错误] 运行异常: {}\n", e.what());
        fmt::print("       若与数据库相关, 请检查项目根目录 .env 中的 MYSQL_* 配置 "
                   "(库 wucai_trade), 以及 MySQL 服务是否可用\n");
        return 1;
    }
}
