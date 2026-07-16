// 21-投资晨会 / CASE-B-板块轮动分析 / run_today.py 的 C++ 实现
// 从 MySQL trade_sector_daily 读取申万板块指数, 输出当日板块轮动综合视图:
//   强度排名 (MOM_21 + RS_60 + VOL_RATIO 横截面 Z-score) + 一/二阶导相位 (五象限)
//   composite_score = score + phase_bonus, 按推荐度排序
//
// 用法: 01_sector_rotation [--level 2] [--end YYYY-MM-DD] [--top 10] [--lookback 90]
// 输出: outputs/week11/sector_combined.csv, sector_strength.csv, sector_phase.csv,
//       sector_today_top{top}.txt, sector_top{top}.png

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <matplot/matplot.h>
#include <mysql.h>

#include "backtest_data_mysql.hpp"
#include "env.hpp"
#include "mysql_client.hpp"
#include "sector_rotation.hpp"

namespace {

namespace sr = quant::sector;

struct CliOptions {
    int level = 2;            // 1=申万一级, 2=申万二级 (默认 2)
    std::string end;          // 截止日 YYYY-MM-DD, 空 = 最新交易日
    int top = 10;             // Top N 输出
    int lookback = 90;        // 强度回看天数 / 图表天数
};

CliOptions parse_args(int argc, char* argv[]) {
    CliOptions opt;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--level" && i + 1 < argc) {
            opt.level = std::atoi(argv[++i]);
        } else if (arg == "--end" && i + 1 < argc) {
            opt.end = argv[++i];
        } else if (arg == "--top" && i + 1 < argc) {
            opt.top = std::atoi(argv[++i]);
        } else if (arg == "--lookback" && i + 1 < argc) {
            opt.lookback = std::atoi(argv[++i]);
        }
    }
    return opt;
}

// 只保留日期字符 (0-9 和 -), 防止 SQL 注入
std::string sanitize_date(const std::string& s) {
    std::string out;
    for (char c : s) {
        if ((c >= '0' && c <= '9') || c == '-') out.push_back(c);
    }
    return out;
}

// UTF-8 字符串显示宽度 (ASCII 算 1, 其余按 CJK 算 2), 用于控制台/报告对齐
size_t display_width(const std::string& s) {
    size_t w = 0;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            w += 1;
            i += 1;
        } else {
            w += 2;
            i += (c >= 0xF0) ? 4 : ((c >= 0xE0) ? 3 : 2);
        }
    }
    return w;
}

std::string pad_right(const std::string& s, size_t width) {
    const size_t w = display_width(s);
    return s + std::string(w < width ? width - w : 0, ' ');
}

// 执行查询并取回结果集, 失败返回 nullptr
MYSQL_RES* query(quant::mysql::Client& client, const std::string& sql) {
    if (mysql_query(client.raw(), sql.c_str()) != 0) return nullptr;
    return mysql_store_result(client.raw());
}

// 查该级别最新交易日
std::string query_max_date(quant::mysql::Client& client, int level) {
    MYSQL_RES* res = query(client, "SELECT MAX(trade_date) FROM trade_sector_daily WHERE sector_level = "
                                   + std::to_string(level));
    if (!res) return "";
    std::string out;
    if (MYSQL_ROW row = mysql_fetch_row(res)) out = row[0] ? row[0] : "";
    mysql_free_result(res);
    return out;
}

// 加载某级别全部板块指数 K 线 (sector_loader.load_all_sectors), 过滤不足 min_days 的板块
sr::SectorPanel load_sector_panel(quant::mysql::Client& client, int level,
                                  const std::string& end_date, size_t min_days = 60) {
    sr::SectorPanel panel;
    std::string sql =
        "SELECT sector_name, trade_date, open_idx, high_idx, low_idx, close_idx, "
        "total_volume, total_amount FROM trade_sector_daily WHERE sector_level = "
        + std::to_string(level);
    if (!end_date.empty()) sql += " AND trade_date <= '" + end_date + "'";
    sql += " ORDER BY sector_name, trade_date";

    MYSQL_RES* res = query(client, sql);
    if (!res) return panel;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        const std::string name = row[0] ? row[0] : "";
        sr::SectorBar bar;
        bar.date = row[1] ? row[1] : "";
        bar.open = row[2] ? std::stod(row[2]) : 0.0;
        bar.high = row[3] ? std::stod(row[3]) : 0.0;
        bar.low = row[4] ? std::stod(row[4]) : 0.0;
        bar.close = row[5] ? std::stod(row[5]) : 0.0;
        bar.volume = row[6] ? std::stod(row[6]) : 0.0;
        bar.amount = row[7] ? std::stod(row[7]) : 0.0;  // 表中有 total_amount 列, VOL_RATIO 用成交额
        panel[name].push_back(bar);
    }
    mysql_free_result(res);

    // Python min_days=60: 历史数据不足的板块过滤掉
    for (auto it = panel.begin(); it != panel.end();) {
        if (it->second.size() < min_days) {
            it = panel.erase(it);
        } else {
            ++it;
        }
    }
    return panel;
}

// 板块成员数 (从 trade_stock_status 反查, sector_loader.list_sectors)
std::map<std::string, int> load_member_counts(quant::mysql::Client& client, int level) {
    std::map<std::string, int> counts;
    const std::string field = (level == 1) ? "sector_1" : "sector_2";
    MYSQL_RES* res = query(client, "SELECT " + field + ", COUNT(*) FROM trade_stock_status WHERE "
                                   + field + " IS NOT NULL GROUP BY " + field);
    if (!res) return counts;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        if (row[0]) counts[row[0]] = row[1] ? std::atoi(row[1]) : 0;
    }
    mysql_free_result(res);
    return counts;
}

int member_count_of(const std::map<std::string, int>& counts, const std::string& sector) {
    const auto it = counts.find(sector);
    return it != counts.end() ? it->second : 0;
}

// ---------------------------------------------------------------
// 控制台输出
// ---------------------------------------------------------------

void print_row_table(const std::vector<sr::StrengthRow>& rows, size_t begin, size_t end) {
    fmt::print("  {:>4}  {}  {:>8}  {}  {:>9}  {:>9}  {:>9}  {:>6}\n",
               "排名", pad_right("板块", 16), "score", pad_right("相位", 12),
               "composite", "MOM_21(%)", "RS_60(%)", "VOL_R");
    for (size_t i = begin; i < end && i < rows.size(); ++i) {
        const auto& r = rows[i];
        fmt::print("  {:>4}  {}  {:>+8.4f}  {}  {:>+9.4f}  {:>+9.2f}  {:>+9.2f}  {:>6.2f}\n",
                   r.rank, pad_right(r.sector, 16), r.score,
                   pad_right(sr::phase_to_string(r.phase), 12), r.composite,
                   r.mom_21 * 100.0, r.rs_60 * 100.0, r.vol_ratio);
    }
}

// ---------------------------------------------------------------
// 落盘: CSV (带 UTF-8 BOM)
// ---------------------------------------------------------------

void write_bom(std::ofstream& ofs) { ofs << "\xEF\xBB\xBF"; }

void save_csvs(const std::vector<sr::StrengthRow>& rows,
               const std::map<std::string, sr::PhaseInfo>& phases,
               const std::map<std::string, int>& member_counts,
               const std::map<std::string, int>& strength_ranks,
               const std::map<std::string, std::array<double, 3>>& zscores,
               const std::string& out_dir) {
    // 1. 综合视图 (对标 sector_combined_today.csv)
    {
        std::ofstream ofs(out_dir + "/sector_combined.csv", std::ios::binary);
        write_bom(ofs);
        ofs << "sector_name,composite_rank,composite_score,score,strength_rank,phase,phase_desc,"
               "phase_bonus,MOM_21,RS_60,VOL_RATIO,MOM_21_z,RS_60_z,VOL_RATIO_z,"
               "ROC_20,MA20_SLOPE,MACD_HIST,MA20_ACCEL,vote_velocity,vote_accel,member_count\n";
        for (const auto& r : rows) {
            const auto& pi = phases.at(r.sector);
            const auto& z = zscores.at(r.sector);
            ofs << r.sector << ',' << r.rank << ',' << fmt::format("{:.4f}", r.composite) << ','
                << fmt::format("{:.4f}", r.score) << ',' << strength_ranks.at(r.sector) << ','
                << sr::phase_to_string(r.phase) << ',' << sr::phase_to_desc(r.phase) << ','
                << fmt::format("{:.1f}", sr::phase_bonus(r.phase)) << ','
                << fmt::format("{:.6f}", r.mom_21) << ',' << fmt::format("{:.6f}", r.rs_60) << ','
                << fmt::format("{:.4f}", r.vol_ratio) << ','
                << fmt::format("{:.4f}", z[0]) << ',' << fmt::format("{:.4f}", z[1]) << ','
                << fmt::format("{:.4f}", z[2]) << ','
                << fmt::format("{:.6f}", pi.roc_20) << ',' << fmt::format("{:.4f}", pi.ma20_slope) << ','
                << fmt::format("{:.6f}", pi.macd_hist) << ',' << fmt::format("{:.4f}", pi.ma20_accel) << ','
                << pi.vote_velocity << ',' << pi.vote_accel << ','
                << member_count_of(member_counts, r.sector) << '\n';
        }
    }
    // 2. 强度单表 (对标 sector_strength_today.csv)
    {
        std::ofstream ofs(out_dir + "/sector_strength.csv", std::ios::binary);
        write_bom(ofs);
        ofs << "sector_name,score,strength_rank,MOM_21,RS_60,VOL_RATIO,"
               "MOM_21_z,RS_60_z,VOL_RATIO_z,member_count\n";
        for (const auto& r : rows) {
            const auto& z = zscores.at(r.sector);
            ofs << r.sector << ',' << fmt::format("{:.4f}", r.score) << ','
                << strength_ranks.at(r.sector) << ','
                << fmt::format("{:.6f}", r.mom_21) << ',' << fmt::format("{:.6f}", r.rs_60) << ','
                << fmt::format("{:.4f}", r.vol_ratio) << ','
                << fmt::format("{:.4f}", z[0]) << ',' << fmt::format("{:.4f}", z[1]) << ','
                << fmt::format("{:.4f}", z[2]) << ','
                << member_count_of(member_counts, r.sector) << '\n';
        }
    }
    // 3. 相位单表 (对标 sector_phase_today.csv, 按综合排名顺序)
    {
        std::ofstream ofs(out_dir + "/sector_phase.csv", std::ios::binary);
        write_bom(ofs);
        ofs << "sector_name,phase,phase_desc,ROC_20,MA20_SLOPE,MACD_HIST,MA20_ACCEL,"
               "vote_velocity,vote_accel\n";
        for (const auto& r : rows) {
            const auto& pi = phases.at(r.sector);
            ofs << r.sector << ',' << sr::phase_to_string(r.phase) << ','
                << sr::phase_to_desc(r.phase) << ','
                << fmt::format("{:.6f}", pi.roc_20) << ',' << fmt::format("{:.4f}", pi.ma20_slope) << ','
                << fmt::format("{:.6f}", pi.macd_hist) << ',' << fmt::format("{:.4f}", pi.ma20_accel) << ','
                << pi.vote_velocity << ',' << pi.vote_accel << '\n';
        }
    }
}

// ---------------------------------------------------------------
// 落盘: 人类可读 Top N 报告 (仿 run_today.py save_today_view)
// ---------------------------------------------------------------

void save_txt_report(const std::vector<sr::StrengthRow>& rows,
                     const std::map<std::string, sr::PhaseInfo>& phases,
                     const std::string& end_date, int level, int top,
                     const std::string& out_dir) {
    const size_t top_n = std::min<size_t>(static_cast<size_t>(top), rows.size());
    std::vector<std::string> lines;
    lines.push_back(std::string(78, '='));
    lines.push_back(fmt::format("  板块轮动晨会 -- {} (申万{}级)", end_date, level == 1 ? "一" : "二"));
    lines.push_back(std::string(78, '='));
    lines.push_back("");

    // [1] Top N 综合推荐 (注意: Python 原版 ROC20/MOM21/RS60 忘乘 100, 这里按正确百分比显示)
    lines.push_back(fmt::format("[1] Top {} 综合推荐板块  (composite_score = strength_z + phase_bonus)", top_n));
    lines.push_back("");
    for (size_t i = 0; i < top_n; ++i) {
        const auto& r = rows[i];
        const auto& pi = phases.at(r.sector);
        lines.push_back(fmt::format("  #{:>2} {}  score={:>+5.2f}  phase={}  ROC20={:>+5.1f}%  "
                                    "MOM21={:>+5.1f}%  RS60={:>+5.1f}%",
                                    i + 1, pad_right(r.sector, 14), r.composite,
                                    pad_right(sr::phase_to_string(r.phase), 11),
                                    pi.roc_20 * 100.0, r.mom_21 * 100.0, r.rs_60 * 100.0));
    }

    // [2] 拐点信号: 主升 (accel_up) + 左侧抄底 (decel_down)
    std::vector<const sr::StrengthRow*> bullish;
    for (const auto& r : rows) {
        if (r.phase == sr::Phase::ACCEL_UP || r.phase == sr::Phase::DECEL_DOWN) bullish.push_back(&r);
    }
    lines.push_back("");
    lines.push_back(fmt::format("[2] 拐点信号: 主升 (accel_up) + 左侧抄底 (decel_down) 共 {} 个",
                                bullish.size()));
    lines.push_back("");
    for (size_t i = 0; i < bullish.size() && i < 10; ++i) {
        const auto& r = *bullish[i];
        const auto& pi = phases.at(r.sector);
        lines.push_back(fmt::format("  #{:>2} {}  {}  ROC20={:>+5.1f}%  MACD_HIST={:>+6.2f}",
                                    i + 1, pad_right(r.sector, 14),
                                    pad_right(sr::phase_to_desc(r.phase), 18),
                                    pi.roc_20 * 100.0, pi.macd_hist));
    }

    // [3] 警示板块 (排名末 5, 从最差开始)
    lines.push_back("");
    lines.push_back("[3] 警示板块 (排名末 5)");
    lines.push_back("");
    const size_t n = rows.size();
    for (size_t k = 0; k < 5 && k < n; ++k) {
        const auto& r = rows[n - 1 - k];
        const auto& pi = phases.at(r.sector);
        lines.push_back(fmt::format("  #{} {}  score={:>+5.2f}  phase={}  ROC20={:>+5.1f}%",
                                    k + 1, pad_right(r.sector, 14), r.composite,
                                    pad_right(sr::phase_to_string(r.phase), 11),
                                    pi.roc_20 * 100.0));
    }

    lines.push_back("");
    lines.push_back(std::string(78, '-'));
    lines.push_back("完整数据见 sector_combined.csv");

    const std::string path = out_dir + "/sector_today_top" + std::to_string(top) + ".txt";
    std::ofstream ofs(path, std::ios::binary);
    for (const auto& line : lines) ofs << line << '\n';
}

// ---------------------------------------------------------------
// 落盘: Top N 板块近 lookback 日归一化净值曲线
// ---------------------------------------------------------------

void save_chart(const std::vector<sr::StrengthRow>& rows, const sr::SectorPanel& panel,
                int top, int lookback, const std::string& end_date,
                const std::string& out_dir) {
    namespace mp = matplot;
    const size_t top_n = std::min<size_t>(static_cast<size_t>(top), rows.size());
    if (top_n == 0) return;

    auto fig = mp::figure(false);
    fig->size(1400, 900);
    auto ax = fig->current_axes();
    ax->hold(mp::on);

    std::vector<std::string> first_dates;  // 第一个板块的窗口日期, 用于 x 轴刻度
    for (size_t k = 0; k < top_n; ++k) {
        const auto it = panel.find(rows[k].sector);
        if (it == panel.end() || it->second.empty()) continue;
        const auto& bars = it->second;  // 已按 --end 过滤且升序
        const size_t len = std::min<size_t>(static_cast<size_t>(lookback), bars.size());
        const size_t start = bars.size() - len;
        std::vector<double> xs(len), nav(len);
        const double base = bars[start].close;
        if (base <= 0.0) continue;
        for (size_t i = 0; i < len; ++i) {
            xs[i] = static_cast<double>(i);
            nav[i] = bars[start + i].close / base;
        }
        ax->plot(xs, nav, "-")->display_name(rows[k].sector);
        if (first_dates.empty()) {
            for (size_t i = start; i < bars.size(); ++i) first_dates.push_back(bars[i].date);
        }
    }

    if (!first_dates.empty()) {
        std::vector<double> tick_x;
        std::vector<std::string> tick_labels;
        const size_t step = std::max<size_t>(1, first_dates.size() / 8);
        for (size_t i = 0; i < first_dates.size(); i += step) {
            tick_x.push_back(static_cast<double>(i));
            tick_labels.push_back(first_dates[i]);
        }
        ax->xticks(tick_x);
        ax->xticklabels(tick_labels);
    }
    ax->xlabel("日期");
    ax->ylabel("归一化净值 (窗口首日 = 1)");
    ax->title(fmt::format("Top {} 板块近 {} 日归一化净值 (截止 {})", top_n, lookback, end_date));
    ax->legend();
    ax->grid(mp::on);

    const std::string path = out_dir + "/sector_top" + std::to_string(top) + ".png";
    fig->save(path);
    fmt::print("    - {}\n", path);
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const CliOptions opt = parse_args(argc, argv);
        if (opt.level != 1 && opt.level != 2) {
            fmt::print("[错误] --level 只支持 1 (申万一级) 或 2 (申万二级)\n");
            return 1;
        }
        if (opt.top <= 0 || opt.lookback <= 0) {
            fmt::print("[错误] --top 和 --lookback 必须为正整数\n");
            return 1;
        }
        const std::string level_name = (opt.level == 1) ? "一" : "二";

        std::filesystem::create_directories("outputs/week11");
        const std::string out_dir = "outputs/week11";

        // ---- MySQL 连接 ----
        auto env = quant::env::find_and_load_dotenv();
        auto cfg = quant::bt::data::load_mysql_config(env);
        quant::mysql::Client client(cfg);
        if (!client.connect()) {
            fmt::print("[错误] MySQL 连接失败: {} (host={} port={} db={})\n",
                       client.last_error(), cfg.host, cfg.port, cfg.database);
            fmt::print("       请检查项目根目录 .env 中的 MYSQL_* / WUCAI_SQL_* 配置, 并确认 MySQL 服务已启动\n");
            return 1;
        }

        // ---- 确定截止日 (默认该级别最新交易日) ----
        std::string end_date = sanitize_date(opt.end);
        if (end_date.empty()) {
            end_date = query_max_date(client, opt.level);
            if (end_date.empty()) {
                fmt::print("[错误] trade_sector_daily 中没有 sector_level={} 的数据\n", opt.level);
                return 1;
            }
        }

        fmt::print("\n[TODAY] 构建板块轮动综合视图 (level={}, 截止 {}, 回看 {} 日) ...\n\n",
                   opt.level, end_date, opt.lookback);

        // ---- 加载板块指数 (截止 end_date 的全部历史) ----
        fmt::print("[STRENGTH] 加载申万{}级所有板块指数 (level={}) ...\n", level_name, opt.level);
        sr::SectorPanel panel = load_sector_panel(client, opt.level, end_date, 60);
        fmt::print("[STRENGTH] 有效板块: {}\n", panel.size());
        if (panel.empty()) {
            fmt::print("[错误] 没有可分析的板块, 请确认 trade_sector_daily 已落库 "
                       "(注意: 当前库中仅有 sector_level=2 的二级板块数据)\n");
            return 1;
        }

        // ---- 市场基准 (全板块等权) ----
        sr::Benchmark bench = sr::build_market_benchmark(panel);
        if (bench.empty()) {
            fmt::print("[错误] 市场基准构建失败 (板块日期无交集)\n");
            return 1;
        }
        const size_t end_idx = sr::find_end_idx(bench, end_date);
        if (end_idx == sr::kNpos) {
            fmt::print("[错误] 截止日 {} 早于基准首个交易日 {}\n", end_date, bench.dates.front());
            return 1;
        }
        const std::string eff_end = bench.dates[end_idx];  // 实际使用的交易日
        if (eff_end != end_date) {
            fmt::print("[提示] {} 非交易日, 使用之前最近交易日 {}\n", end_date, eff_end);
        }

        // ---- 强度排名 ----
        auto rows = sr::calc_strength(panel, bench, end_idx, static_cast<size_t>(opt.lookback));
        if (rows.empty()) {
            fmt::print("[错误] 没有可排名的板块 (数据窗口不足 70 日)\n");
            return 1;
        }

        // 快照强度排名和 Z-score (供 CSV 输出, 对标 Python 的 rank 和 *_z 列)
        std::map<std::string, int> strength_ranks;
        std::map<std::string, std::array<double, 3>> zscores;
        {
            std::vector<double> mom_v, rs_v, vr_v;
            for (const auto& r : rows) {
                strength_ranks[r.sector] = r.rank;
                mom_v.push_back(r.mom_21);
                rs_v.push_back(r.rs_60);
                vr_v.push_back(r.vol_ratio);
            }
            const auto zm = sr::zscore(mom_v);
            const auto zr = sr::zscore(rs_v);
            const auto zv = sr::zscore(vr_v);
            for (size_t i = 0; i < rows.size(); ++i) {
                zscores[rows[i].sector] = {zm[i], zr[i], zv[i]};
            }
        }

        // ---- 相位检测 (用截止日的全部历史 close, 与 Python 一致) ----
        std::map<std::string, sr::PhaseInfo> phases;
        for (auto& r : rows) {
            sr::PhaseInfo info = sr::detect_phase(panel.at(r.sector));
            if (!info.valid) info.phase = sr::Phase::NEUTRAL;  // 样本不足按中性 (左 join 缺失填 neutral)
            r.phase = info.phase;
            phases[r.sector] = info;
        }

        // ---- 综合评分 + 排名 ----
        sr::apply_composite(rows);

        // ---- 控制台输出 ----
        fmt::print("\n{0}\n", std::string(78, '='));
        fmt::print("  申万{}级板块轮动综合视图 (截止 {}, 回看 {} 日, 有效板块 {} 个)\n",
                   level_name, eff_end, opt.lookback, rows.size());
        fmt::print("{0}\n\n", std::string(78, '='));

        const size_t top_n = std::min<size_t>(static_cast<size_t>(opt.top), rows.size());
        fmt::print("[Top {} 综合推荐板块]  (composite = strength_z + phase_bonus)\n", top_n);
        print_row_table(rows, 0, top_n);

        fmt::print("\n[相位分组统计]\n");
        for (sr::Phase p : sr::phase_display_order()) {
            size_t cnt = 0;
            for (const auto& r : rows) {
                if (r.phase == p) ++cnt;
            }
            fmt::print("  {} ({}): {} 个 ({:.1f}%)\n",
                       pad_right(sr::phase_to_string(p), 11), sr::phase_to_desc(p), cnt,
                       rows.empty() ? 0.0 : cnt * 100.0 / static_cast<double>(rows.size()));
        }

        fmt::print("\n[Bottom 5 弱势板块]\n");
        print_row_table(rows, rows.size() > 5 ? rows.size() - 5 : 0, rows.size());

        // ---- 落盘 ----
        const std::map<std::string, int> member_counts = load_member_counts(client, opt.level);
        save_csvs(rows, phases, member_counts, strength_ranks, zscores, out_dir);
        save_txt_report(rows, phases, eff_end, opt.level, opt.top, out_dir);

        fmt::print("\n[OK] 当日视图已落盘: {}\n", out_dir);
        fmt::print("    - {}/sector_combined.csv\n", out_dir);
        fmt::print("    - {}/sector_strength.csv\n", out_dir);
        fmt::print("    - {}/sector_phase.csv\n", out_dir);
        fmt::print("    - {}/sector_today_top{}.txt\n", out_dir, opt.top);
        save_chart(rows, panel, opt.top, opt.lookback, eff_end, out_dir);
        fmt::print("\n");
        return 0;
    } catch (const std::exception& e) {
        fmt::print("[错误] 程序异常: {}\n", e.what());
        return 1;
    }
}
