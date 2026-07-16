// 21-投资晨会 / CASE-C-多因子选股/layered_backtest.py 的 C++ 实现
// ============================================================================
// 多因子选股分层回测:
//   每 21 个交易日为调仓日 -> 截断算因子快照 -> MAD 去极值 -> Z-score
//   -> 行业中性化 -> 记录单因子 IC (因子值 vs 下期收益)
//   -> 等权 或 IC 加权 (前 ic_lookback 期 IC 均值定权, walk-forward) 合成 alpha
//   -> 按 alpha 等频分 5 层 (qcut) -> 各层下期等权收益
//   -> 累计净值/年化/MDD/Sharpe -> IC 均值/IR -> 多空 L5-L1
//   -> Top-N(5/10/20) 集中度 -> 与等权基准对照
//
// 与 Python 版的口径差异 (有意为之):
//   1. 基准: Python 用沪深 300 指数 (xtdata, 不可得), 本版用"池内全部股票
//      等权下期收益"合成穷人版基准, 与 Python calc_benchmark_returns 的
//      dict 模式口径一致; 超额收益的基准口径与原版不同, 解读时需注意.
//   2. 价格: Python 用 xtdata 后复权价 (dividend_type="back"), 本版用
//      MySQL trade_stock_daily 未复权价, 分红除权附近收益有轻微失真.
//   3. 行业: Python 用 xtdata 行业, 本版用 trade_stock_status.sector_1.
//   4. 停牌: 按日历日期对齐, 调仓日/到期日须当日有成交才计入下期收益
//     (Python 假定 xtdata 面板已按 iloc 对齐, NaN 行自然剔除).
//   5. 输出 CSV 中的单因子 IC 只含"完整走完流程"的调仓期 (便于列对齐);
//      用于 IC 加权定权的历史序列与 Python 一致, 含个别被跳过的期 (极少触发).
//   6. TURN_20 / Z-score ddof 差异见 common/factor_lib.hpp 头注释.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <matplot/matplot.h>
#include <nlohmann/json.hpp>

#include "backtest_data_mysql.hpp"  // load_mysql_config
#include "env.hpp"
#include "factor_lib.hpp"
#include "mysql_client.hpp"

using json = nlohmann::json;
using quant::factor::FactorRows;
using quant::factor::KlineRec;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();

// ---------------------------------------------------------------------------
// CLI 参数
// ---------------------------------------------------------------------------
struct Args {
    std::string pool = "data/csi300_codes.txt";  // 股票池快照 (一行一个代码, 无表头)
    int max_stocks = 80;     // 只用池内前 N 只 (控制规模, 跑通后可调大)
    int lookback = 400;      // 每股拉多少根日 K (Python xtdata count 语义)
    int rebal = 21;          // 调仓周期 (日), 21 = 月度
    int ic_lookback = 6;     // IC 加权用前几期 IC 估权重
    std::string weight = "equal";  // equal | ic (ic_weighted 也接受)
    std::string start;       // 起始日期 (yyyy-mm-dd 或 yyyymmdd), 空 = 不限
    std::string end;         // 截止日期, 空 = 最新
};

// 接受 yyyymmdd / yyyy-mm-dd, 统一成 yyyy-mm-dd
std::string norm_date(const std::string& s) {
    std::string d;
    for (char c : s) {
        if (c >= '0' && c <= '9') d.push_back(c);
    }
    if (d.size() == 8) return d.substr(0, 4) + "-" + d.substr(4, 2) + "-" + d.substr(6, 2);
    return s;
}

Args parse_args(int argc, char* argv[]) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(fmt::format("参数 {} 缺少取值", name));
            return argv[++i];
        };
        if (arg == "--pool") a.pool = next("--pool");
        else if (arg == "--max-stocks") a.max_stocks = std::stoi(next("--max-stocks"));
        else if (arg == "--lookback") a.lookback = std::stoi(next("--lookback"));
        else if (arg == "--rebal") a.rebal = std::stoi(next("--rebal"));
        else if (arg == "--ic-lookback") a.ic_lookback = std::stoi(next("--ic-lookback"));
        else if (arg == "--weight") a.weight = next("--weight");
        else if (arg == "--start") a.start = norm_date(next("--start"));
        else if (arg == "--end") a.end = norm_date(next("--end"));
        else throw std::runtime_error(fmt::format("未知参数: {}", arg));
    }
    if (a.weight == "ic_weighted") a.weight = "ic";
    if (a.weight != "equal" && a.weight != "ic") {
        throw std::runtime_error(fmt::format("--weight 只支持 equal | ic, 收到: {}", a.weight));
    }
    if (a.max_stocks <= 0 || a.lookback <= 0 || a.rebal <= 0 || a.ic_lookback <= 0) {
        throw std::runtime_error("--max-stocks/--lookback/--rebal/--ic-lookback 必须为正整数");
    }
    return a;
}

// ---------------------------------------------------------------------------
// 数据加载
// ---------------------------------------------------------------------------

// 读股票池文件 (一行一个 000001.SZ, 无表头), 取前 max_stocks 只
std::vector<std::string> read_pool(const std::string& path, int max_stocks) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error(fmt::format(
            "股票池文件不存在: {} (可用 --pool 指定路径; 期望格式: 一行一个股票代码, 无表头)", path));
    }
    std::vector<std::string> codes;
    std::string line;
    while (std::getline(ifs, line)) {
        // trim (兼容 \r\n 与首尾空白)
        size_t b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t\r\n");
        codes.push_back(line.substr(b, e - b + 1));
    }
    if (codes.empty()) {
        throw std::runtime_error(fmt::format("股票池文件为空: {}", path));
    }
    if (static_cast<int>(codes.size()) > max_stocks) codes.resize(static_cast<size_t>(max_stocks));
    return codes;
}

double to_double(const char* s, double def) {
    if (!s || !s[0]) return def;
    try {
        return std::stod(s);
    } catch (...) {
        return def;
    }
}

// 分批 WHERE stock_code IN (...) 加载池内股票日线 (含 amount / turnover_rate),
// 每股只保留尾部 lookback 根 (Python xtdata count=lookback 语义); 循环外一次加载, 循环内不发 SQL
std::map<std::string, std::vector<KlineRec>> load_klines(
    quant::mysql::Client& client,
    const std::vector<std::string>& codes,
    const std::string& start,
    const std::string& end,
    size_t lookback) {
    std::map<std::string, std::vector<KlineRec>> result;
    const size_t kChunk = 100;
    for (size_t base = 0; base < codes.size(); base += kChunk) {
        std::ostringstream in;
        for (size_t i = base; i < std::min(base + kChunk, codes.size()); ++i) {
            if (i > base) in << ",";
            in << "'" << codes[i] << "'";
        }
        std::ostringstream sql;
        sql << "SELECT stock_code, trade_date, open_price, high_price, low_price, close_price,"
            << " volume, amount, turnover_rate FROM trade_stock_daily"
            << " WHERE stock_code IN (" << in.str() << ")";
        if (!start.empty()) sql << " AND trade_date >= '" << start << "'";
        if (!end.empty()) sql << " AND trade_date <= '" << end << "'";
        sql << " ORDER BY stock_code, trade_date ASC";
        if (mysql_query(client.raw(), sql.str().c_str()) != 0) {
            throw std::runtime_error(fmt::format("查询 K 线失败: {}", client.last_error()));
        }
        MYSQL_RES* res = mysql_store_result(client.raw());
        if (!res) throw std::runtime_error("查询 K 线失败: 无法获取结果集");
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            KlineRec k;
            std::string code = row[0] ? row[0] : "";
            k.date = row[1] ? row[1] : "";
            k.open = to_double(row[2], 0.0);
            k.high = to_double(row[3], 0.0);
            k.low = to_double(row[4], 0.0);
            k.close = to_double(row[5], 0.0);
            k.volume = to_double(row[6], 0.0);
            k.amount = to_double(row[7], kNaN);           // NULL -> NaN (缺失)
            k.turnover_rate = to_double(row[8], kNaN);    // NULL -> NaN (缺失)
            result[code].push_back(k);
        }
        mysql_free_result(res);
    }
    for (auto& kv : result) {
        auto& v = kv.second;
        if (v.size() > lookback) {
            v.erase(v.begin(), v.end() - static_cast<std::ptrdiff_t>(lookback));
        }
    }
    return result;
}

// 行业映射: trade_stock_status.sector_1 (中文一级行业名, 连接已设 utf8mb4)
std::unordered_map<std::string, std::string> load_industry_map(
    quant::mysql::Client& client,
    const std::vector<std::string>& codes) {
    std::unordered_map<std::string, std::string> out;
    const size_t kChunk = 100;
    for (size_t base = 0; base < codes.size(); base += kChunk) {
        std::ostringstream in;
        for (size_t i = base; i < std::min(base + kChunk, codes.size()); ++i) {
            if (i > base) in << ",";
            in << "'" << codes[i] << "'";
        }
        std::string sql = "SELECT stock_code, sector_1 FROM trade_stock_status"
                          " WHERE stock_code IN (" + in.str() + ")";
        if (mysql_query(client.raw(), sql.c_str()) != 0) {
            throw std::runtime_error(fmt::format("查询行业失败: {}", client.last_error()));
        }
        MYSQL_RES* res = mysql_store_result(client.raw());
        if (!res) throw std::runtime_error("查询行业失败: 无法获取结果集");
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            std::string code = row[0] ? row[0] : "";
            std::string sector = row[1] ? row[1] : "";
            if (!code.empty()) out.emplace(code, sector);  // first wins
        }
        mysql_free_result(res);
    }
    return out;
}

// ---------------------------------------------------------------------------
// 分层回测引擎 (对应 layered_backtest.py::run_layered_backtest)
// ---------------------------------------------------------------------------

struct LayeredResult {
    std::vector<std::string> dates;                        // 调仓日
    std::vector<std::vector<double>> layer_rets;           // [期][5] 各层下期等权收益
    std::vector<double> long_short;                        // L5 - L1
    std::vector<double> bench_rets;                        // 穷人版基准: 池内全部股票等权下期收益
    std::vector<double> alpha_ic;                          // 合成 alpha 的当期 IC
    std::vector<std::map<std::string, double>> factor_ics; // [期]{因子: IC}
    std::vector<std::map<std::string, double>> topn_rets;  // [期]{"Top5": r, ...}
};

LayeredResult run_layered_backtest(
    const std::map<std::string, std::vector<KlineRec>>& panel,
    const std::unordered_map<std::string, std::string>& industry_map,
    const std::vector<std::string>& calendar,
    int rebal,
    int ic_lookback,
    const std::string& weight_method) {
    const size_t n_layers = 5;
    const size_t min_warmup = 130;  // 前 130 个交易日 warm up (与 Python 一致)
    const size_t n = calendar.size();
    if (n < min_warmup + static_cast<size_t>(rebal) * 2) {
        throw std::runtime_error(
            fmt::format("数据不足, 至少需要 {} 个交易日, 实际 {}", min_warmup + rebal * 2, n));
    }

    LayeredResult out;
    // walk-forward 单因子 IC 历史 (用于 IC 加权定权)
    std::map<std::string, std::vector<double>> factor_ic_hist;
    // 每股 date -> close 映射 (精确取调仓日/到期日收盘, 处理停牌)
    std::map<std::string, std::unordered_map<std::string, double>> close_by_date;
    for (const auto& kv : panel) {
        auto& m = close_by_date[kv.first];
        m.reserve(kv.second.size());
        for (const auto& k : kv.second) m[k.date] = k.close;
    }

    // 调仓日: 每隔 rebal 一个 (Python: range(min_warmup, n - rebal, rebal))
    const size_t total_rebals = (n - static_cast<size_t>(rebal) - 1 - min_warmup) /
                                static_cast<size_t>(rebal) + 1;
    size_t done = 0;
    for (size_t e = min_warmup; e + static_cast<size_t>(rebal) < n; e += static_cast<size_t>(rebal)) {
        const std::string& d0 = calendar[e];
        const std::string& d1 = calendar[e + static_cast<size_t>(rebal)];

        // 1) 因子快照: 每股截取 date <= d0 的历史 K 线算 10 因子
        FactorRows raw;
        for (const auto& kv : panel) {
            const auto& ks = kv.second;
            size_t cnt = static_cast<size_t>(
                std::upper_bound(ks.begin(), ks.end(), d0,
                                 [](const std::string& d, const KlineRec& k) { return d < k.date; }) -
                ks.begin());
            if (cnt < 130) continue;  // Python: len(df) < 130 剔除
            std::vector<KlineRec> sub(ks.begin(), ks.begin() + static_cast<std::ptrdiff_t>(cnt));
            auto f = quant::factor::calc_factors(sub);
            if (!f.empty()) raw[kv.first] = std::move(f);
        }
        if (raw.size() < n_layers * 5) continue;  // Python: len(factor_df) < n_layers*5

        // 2) 预处理三件套 (去极值 + Z-score + 行业中性化)
        FactorRows proc = quant::factor::preprocess(raw, industry_map, 3.0, true);

        // 3) 下期收益: d0 -> d1 精确日期收盘 (停牌缺一天即剔除, 对应 Python NaN 剔除)
        std::map<std::string, double> fwd;
        for (const auto& kv : panel) {
            const auto& m = close_by_date[kv.first];
            auto it0 = m.find(d0);
            auto it1 = m.find(d1);
            if (it0 == m.end() || it1 == m.end()) continue;
            if (it0->second > 0.0) fwd[kv.first] = it1->second / it0->second - 1.0;
        }

        // 3.1) 单因子 IC (预处理后的因子值 vs 下期收益, spearman)
        std::map<std::string, double> ic_now;
        for (const auto& fname : quant::factor::factor_names()) {
            std::vector<double> fv, rv;
            for (const auto& kv : proc) {
                double v = quant::factor::get_factor(kv.second, fname);
                auto it = fwd.find(kv.first);
                if (std::isnan(v) || it == fwd.end()) continue;
                fv.push_back(v);
                rv.push_back(it->second);
            }
            ic_now[fname] = quant::factor::calc_ic(fv, rv, "spearman");
        }

        // 3.2) 合成 alpha
        std::map<std::string, double> alpha;
        if (weight_method == "ic") {
            // 用前 ic_lookback 期 IC 均值定权 (严格 walk-forward, 不含当期)
            std::map<std::string, double> past_ic;
            for (const auto& kv : factor_ic_hist) {
                const auto& hist = kv.second;
                size_t begin = hist.size() > static_cast<size_t>(ic_lookback)
                                   ? hist.size() - static_cast<size_t>(ic_lookback)
                                   : 0;
                double sum = 0.0;
                size_t cnt = 0;
                for (size_t i = begin; i < hist.size(); ++i) {
                    if (!std::isnan(hist[i])) {
                        sum += hist[i];
                        ++cnt;
                    }
                }
                if (cnt > 0) past_ic[kv.first] = sum / static_cast<double>(cnt);
            }
            bool any = false;
            for (const auto& kv : past_ic) {
                if (kv.second != 0.0) any = true;
            }
            // 前 ic_lookback 期没有历史 IC 时退化为等权 (避免冷启动空跑)
            alpha = any ? quant::factor::combine_ic_weight(proc, past_ic)
                        : quant::factor::combine_equal(proc);
        } else {
            alpha = quant::factor::combine_equal(proc);
        }
        // 当期单因子 IC 追加进历史 (在定权之后, 保证不用未来数据)
        for (const auto& kv : ic_now) factor_ic_hist[kv.first].push_back(kv.second);

        // 剔除 NaN alpha (Python: .dropna())
        for (auto it = alpha.begin(); it != alpha.end();) {
            if (std::isnan(it->second)) it = alpha.erase(it);
            else ++it;
        }
        if (alpha.size() < n_layers * 5) continue;

        // 4) 合成 IC + 取 alpha 与下期收益的交集
        std::vector<std::string> common;
        std::vector<double> av, rv;
        for (const auto& kv : alpha) {
            auto it = fwd.find(kv.first);
            if (it == fwd.end()) continue;
            common.push_back(kv.first);
            av.push_back(kv.second);
            rv.push_back(it->second);
        }
        if (common.size() < n_layers * 5) continue;
        double ic = quant::factor::calc_ic(av, rv, "spearman");

        // 5) 等频分 5 层 (pandas qcut 的秩实现: 按 alpha 升序排名, 每层约 N/5 只;
        //    同分按代码序稳定处理, 与 qcut(duplicates="drop") 的边界行为略有差异)
        std::vector<size_t> ord(common.size());
        std::iota(ord.begin(), ord.end(), 0);
        std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
            if (av[a] != av[b]) return av[a] < av[b];
            return common[a] < common[b];
        });
        std::vector<double> layer_ret(n_layers, 0.0);
        std::vector<size_t> layer_cnt(n_layers, 0);
        for (size_t r = 0; r < common.size(); ++r) {
            size_t layer = std::min(n_layers - 1, r * n_layers / common.size());
            layer_ret[layer] += rv[ord[r]];
            layer_cnt[layer]++;
        }
        for (size_t l = 0; l < n_layers; ++l) {
            layer_ret[l] = layer_cnt[l] > 0
                               ? layer_ret[l] / static_cast<double>(layer_cnt[l])
                               : 0.0;  // Python: layer_means.get(i, 0)
        }
        double ls = layer_ret[n_layers - 1] - layer_ret[0];

        // 6) Top-N 集中度: 按 alpha 降序前 N 等权
        std::map<std::string, double> topn;
        for (int k : {5, 10, 20}) {
            std::string key = fmt::format("Top{}", k);
            if (static_cast<size_t>(k) <= common.size()) {
                double sum = 0.0;
                for (size_t j = 0; j < static_cast<size_t>(k); ++j) {
                    sum += rv[ord[common.size() - 1 - j]];
                }
                topn[key] = sum / static_cast<double>(k);
            } else {
                topn[key] = kNaN;
            }
        }

        // 7) 穷人版基准: 池内全部有效下期收益的等权平均
        //    (Python calc_benchmark_returns dict 模式同款; 原版为沪深 300 指数)
        double bench = kNaN;
        if (!fwd.empty()) {
            double sum = 0.0;
            for (const auto& kv : fwd) sum += kv.second;
            bench = sum / static_cast<double>(fwd.size());
        }

        out.dates.push_back(d0);
        out.layer_rets.push_back(layer_ret);
        out.long_short.push_back(ls);
        out.bench_rets.push_back(bench);
        out.alpha_ic.push_back(ic);
        out.factor_ics.push_back(ic_now);
        out.topn_rets.push_back(topn);

        ++done;
        if (done % 5 == 0) {
            fmt::print("  ... 进度 {}/{}: date={}, IC={:.3f}\n", done, total_rebals, d0, ic);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 指标计算
// ---------------------------------------------------------------------------

// 净值序列指标 (periods_per_year = 250/rebal, 用于年化与 Sharpe)
struct SeriesStats {
    double total_ret = 0.0;
    double annual_ret = 0.0;
    double max_dd = 0.0;
    double sharpe = kNaN;
    double avg_ret = 0.0;
    double final_nav = 1.0;
};

SeriesStats calc_series_stats(const std::vector<double>& period_rets, double periods_per_year) {
    SeriesStats st;
    double nav = 1.0, peak = 1.0, mdd = 0.0;
    for (double r : period_rets) {
        nav *= (1.0 + r);
        peak = std::max(peak, nav);
        mdd = std::min(mdd, nav / peak - 1.0);
    }
    st.final_nav = nav;
    st.total_ret = nav - 1.0;
    st.max_dd = mdd;
    size_t t = period_rets.size();
    if (t > 0 && nav > 0.0) {
        st.annual_ret = std::pow(nav, periods_per_year / static_cast<double>(t)) - 1.0;
    }
    if (t > 0) {
        double mean = std::accumulate(period_rets.begin(), period_rets.end(), 0.0) /
                      static_cast<double>(t);
        st.avg_ret = mean;
        if (t > 1) {
            double ss = 0.0;
            for (double r : period_rets) ss += (r - mean) * (r - mean);
            double sd = std::sqrt(ss / static_cast<double>(t - 1));
            if (sd > 0.0) st.sharpe = mean / sd * std::sqrt(periods_per_year);
        }
    }
    return st;
}

// 累计净值曲线 (cumprod(1+r), 不含初始 1.0)
std::vector<double> cum_nav(const std::vector<double>& rets) {
    std::vector<double> nav(rets.size(), 1.0);
    double acc = 1.0;
    for (size_t i = 0; i < rets.size(); ++i) {
        acc *= (1.0 + rets[i]);
        nav[i] = acc;
    }
    return nav;
}

double round4(double x) { return std::round(x * 10000.0) / 10000.0; }

// NaN -> nullptr (JSON 输出 null)
json num_or_null(double x, bool do_round = true) {
    if (std::isnan(x)) return json(nullptr);
    return json(do_round ? round4(x) : x);
}

// ---------------------------------------------------------------------------
// 输出: CSV / JSON / PNG
// ---------------------------------------------------------------------------

void write_nav_csv(const std::string& path,
                   const LayeredResult& res,
                   const std::vector<std::vector<double>>& layer_navs,
                   const std::vector<double>& bench_nav,
                   const std::vector<double>& ls_nav) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) throw std::runtime_error(fmt::format("无法写入: {}", path));
    ofs << "\xEF\xBB\xBF";  // UTF-8 BOM, 方便 Excel 直接打开
    ofs << "date,L1,L2,L3,L4,L5,benchmark,long_short\n";
    for (size_t i = 0; i < res.dates.size(); ++i) {
        ofs << res.dates[i];
        for (size_t l = 0; l < 5; ++l) ofs << fmt::format(",{:.6f}", layer_navs[l][i]);
        ofs << fmt::format(",{:.6f}", bench_nav[i]);
        ofs << fmt::format(",{:.6f}", ls_nav[i]);
        ofs << "\n";
    }
}

void write_ic_csv(const std::string& path, const LayeredResult& res) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) throw std::runtime_error(fmt::format("无法写入: {}", path));
    ofs << "\xEF\xBB\xBF";  // UTF-8 BOM
    ofs << "date,alpha";
    for (const auto& fname : quant::factor::factor_names()) ofs << "," << fname;
    ofs << "\n";
    auto fmt_ic = [](double v) { return std::isnan(v) ? "" : fmt::format("{:.6f}", v); };
    for (size_t i = 0; i < res.dates.size(); ++i) {
        ofs << res.dates[i] << "," << fmt_ic(res.alpha_ic[i]);
        for (const auto& fname : quant::factor::factor_names()) {
            auto it = res.factor_ics[i].find(fname);
            ofs << "," << fmt_ic(it == res.factor_ics[i].end() ? kNaN : it->second);
        }
        ofs << "\n";
    }
}

void save_layer_chart(const std::string& path,
                      const LayeredResult& res,
                      const std::vector<std::vector<double>>& layer_navs,
                      const std::vector<double>& bench_nav,
                      const std::vector<double>& ls_nav,
                      const std::string& weight_label) {
    namespace mp = matplot;
    const size_t t = res.dates.size();
    std::vector<double> xs(t + 1);
    std::iota(xs.begin(), xs.end(), 0.0);
    // 曲线前置初始净值 1.0, 方便观察
    auto with_start = [&](const std::vector<double>& nav) {
        std::vector<double> y(t + 1, 1.0);
        for (size_t i = 0; i < t; ++i) y[i + 1] = nav[i];
        return y;
    };

    auto fig = mp::figure(false);
    fig->size(1400, 800);
    auto ax = mp::gca();
    ax->hold(mp::on);
    const char* layer_names[5] = {"L1(alpha最低)", "L2", "L3", "L4", "L5(alpha最高)"};
    for (size_t l = 0; l < 5; ++l) {
        ax->plot(xs, with_start(layer_navs[l]), "-")->display_name(layer_names[l]);
    }
    ax->plot(xs, with_start(bench_nav), "--")->display_name("基准(池内等权)");
    ax->plot(xs, with_start(ls_nav), "-.")->display_name("多空(L5-L1)");

    // x 轴稀疏标注调仓日期
    std::vector<double> tick_pos;
    std::vector<std::string> tick_labels;
    size_t step = std::max<size_t>(1, t / 8);
    for (size_t pos = 1; pos <= t; pos += step) {
        tick_pos.push_back(static_cast<double>(pos));
        tick_labels.push_back(res.dates[pos - 1]);
    }
    ax->xticks(tick_pos);
    ax->xticklabels(tick_labels);
    ax->xlabel("调仓日");
    ax->ylabel("累计净值");
    ax->title(fmt::format("多因子分层回测净值 ({})", weight_label));
    ax->legend();
    ax->grid(mp::on);
    fig->save(path);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

void print_single_factor_ic(const std::vector<std::map<std::string, double>>& factor_ics) {
    struct Row {
        std::string name;
        double mean, sd, ir, pos_ratio;
        size_t samples;
    };
    std::vector<Row> rows;
    for (const auto& fname : quant::factor::factor_names()) {
        std::vector<double> ics;
        for (const auto& m : factor_ics) {
            auto it = m.find(fname);
            if (it != m.end() && !std::isnan(it->second)) ics.push_back(it->second);
        }
        if (ics.empty()) continue;
        double mean = std::accumulate(ics.begin(), ics.end(), 0.0) / static_cast<double>(ics.size());
        double sd = 0.0;
        if (ics.size() > 1) {
            double ss = 0.0;
            for (double x : ics) ss += (x - mean) * (x - mean);
            sd = std::sqrt(ss / static_cast<double>(ics.size() - 1));
        }
        size_t pos = 0;
        for (double x : ics) {
            if (x > 0.0) ++pos;
        }
        rows.push_back({fname, mean, sd, sd > 0.0 ? mean / sd : 0.0,
                        static_cast<double>(pos) / static_cast<double>(ics.size()), ics.size()});
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const Row& a, const Row& b) { return a.ir > b.ir; });
    fmt::print("\n[单因子 IC 排名] (按 IR 降序, spearman)\n");
    fmt::print("  {:<8}{:>10}{:>10}{:>10}{:>10}{:>8}\n", "因子", "IC均值", "ICstd", "IR", "正比例", "样本");
    for (const auto& r : rows) {
        fmt::print("  {:<8}{:>+10.4f}{:>10.4f}{:>+10.3f}{:>9.1f}%{:>8}\n",
                   r.name, r.mean, r.sd, r.ir, r.pos_ratio * 100.0, r.samples);
    }
    fmt::print("  解读: IR > 0.5 = 优秀因子 | IR > 0.2 = 可用 | < 0.1 = 噪音\n");
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        Args args = parse_args(argc, argv);
        std::filesystem::create_directories("outputs/week11");

        fmt::print("\n{0}\n", std::string(70, '='));
        fmt::print("  21 章 CASE-C: 多因子选股分层回测 (C++ 版)\n");
        fmt::print("{0}\n\n", std::string(70, '='));

        // [1] 股票池
        auto codes = read_pool(args.pool, args.max_stocks);
        fmt::print("[1/5] 股票池: {} -> 参与 {} 只 (上限 {})\n", args.pool, codes.size(), args.max_stocks);

        // MySQL 连接
        auto env = quant::env::find_and_load_dotenv();
        auto cfg = quant::bt::data::load_mysql_config(env);
        quant::mysql::Client client(cfg);
        if (!client.connect()) {
            fmt::print(stderr, "[错误] MySQL 连接失败: {} (host={} port={} db={})\n",
                       client.last_error(), cfg.host, cfg.port, cfg.database);
            fmt::print(stderr, "  请检查 .env 中的 MYSQL_* / WUCAI_SQL_* 配置, 以及 MySQL 服务是否启动\n");
            return 1;
        }

        // [2] 行业映射
        auto industry_map = load_industry_map(client, codes);
        {
            std::map<std::string, int> ind_count;
            for (const auto& c : codes) {
                auto it = industry_map.find(c);
                if (it != industry_map.end() && !it->second.empty()) ind_count[it->second]++;
            }
            std::vector<std::pair<std::string, int>> top(ind_count.begin(), ind_count.end());
            std::stable_sort(top.begin(), top.end(),
                             [](const auto& a, const auto& b) { return a.second > b.second; });
            fmt::print("[2/5] 行业映射 (sector_1): {}/{} 只有行业, Top 5: ",
                       industry_map.size(), codes.size());
            for (size_t i = 0; i < std::min<size_t>(5, top.size()); ++i) {
                fmt::print("{} x{}{}", top[i].first, top[i].second,
                           i + 1 < std::min<size_t>(5, top.size()) ? ", " : "\n");
            }
            if (top.empty()) fmt::print("(无)\n");
        }

        // [3] K 线 (一次性加载, 循环内不发 SQL)
        fmt::print("[3/5] 加载 K 线 (每股尾部 {} 根{}{}) ...\n", args.lookback,
                   args.start.empty() ? "" : fmt::format(", 起始 {}", args.start),
                   args.end.empty() ? "" : fmt::format(", 截止 {}", args.end));
        auto all = load_klines(client, codes, args.start, args.end,
                               static_cast<size_t>(args.lookback));
        // Python: len(df) > 200 才进入 prices_panel
        std::map<std::string, std::vector<KlineRec>> panel;
        for (auto& kv : all) {
            if (kv.second.size() > 200) panel[kv.first] = std::move(kv.second);
        }
        fmt::print("  实际有效股票: {} ({} 只数据不足 200 根被剔除)\n",
                   panel.size(), all.size() - panel.size());
        if (panel.size() < 25) {
            fmt::print(stderr, "[错误] 有效股票不足 25 只 (5 层 x 5), 无法分层\n");
            return 1;
        }

        // 主日历 = 池内全部交易日期的并集 (升序)
        std::set<std::string> cal_set;
        for (const auto& kv : panel) {
            for (const auto& k : kv.second) cal_set.insert(k.date);
        }
        std::vector<std::string> calendar(cal_set.begin(), cal_set.end());
        fmt::print("  交易日历: {} 天, {} ~ {}\n", calendar.size(), calendar.front(), calendar.back());

        // [4] 分层回测
        std::string weight_label = args.weight == "ic" ? "IC 加权" : "等权";
        fmt::print("[4/5] 分层回测: 调仓 {} 日, 合成方式={} (walk-forward, ic_lookback={})\n",
                   args.rebal, weight_label, args.ic_lookback);
        auto res = run_layered_backtest(panel, industry_map, calendar,
                                        args.rebal, args.ic_lookback, args.weight);
        if (res.dates.empty()) {
            fmt::print(stderr, "[错误] 没有有效调仓期 (数据不足?)\n");
            return 1;
        }
        const size_t T = res.dates.size();
        const double ppy = 250.0 / static_cast<double>(args.rebal);  // 年化用期数/年
        fmt::print("  有效调仓期: {} ({} ~ {})\n", T, res.dates.front(), res.dates.back());

        // 各层 / 基准 / 多空 净值与指标
        std::vector<std::vector<double>> layer_rets_T(5, std::vector<double>(T));
        for (size_t i = 0; i < T; ++i) {
            for (size_t l = 0; l < 5; ++l) layer_rets_T[l][i] = res.layer_rets[i][l];
        }
        std::vector<std::vector<double>> layer_navs(5);
        std::vector<SeriesStats> layer_stats(5);
        for (size_t l = 0; l < 5; ++l) {
            layer_navs[l] = cum_nav(layer_rets_T[l]);
            layer_stats[l] = calc_series_stats(layer_rets_T[l], ppy);
        }
        auto bench_nav = cum_nav(res.bench_rets);
        auto bench_stats = calc_series_stats(res.bench_rets, ppy);
        auto ls_nav = cum_nav(res.long_short);
        auto ls_stats = calc_series_stats(res.long_short, ppy);

        // [5] 输出
        fmt::print("[5/5] 汇总输出 ...\n");
        fmt::print("\n[分层收益表] (5 层等频分层, L5 = alpha 最高)\n");
        fmt::print("  {:<16}{:>12}{:>12}{:>12}{:>10}{:>12}\n",
                   "层级", "累计收益", "年化收益", "最大回撤", "Sharpe", "每期均值");
        const char* lnames[5] = {"L1(alpha最低)", "L2", "L3", "L4", "L5(alpha最高)"};
        for (size_t l = 0; l < 5; ++l) {
            const auto& st = layer_stats[l];
            fmt::print("  {:<16}{:>+11.2f}%{:>+11.2f}%{:>+11.2f}%{:>10.3f}{:>+11.2f}%\n",
                       lnames[l], st.total_ret * 100.0, st.annual_ret * 100.0,
                       st.max_dd * 100.0, st.sharpe, st.avg_ret * 100.0);
        }
        fmt::print("  {:<16}{:>+11.2f}%{:>+11.2f}%{:>+11.2f}%{:>10.3f}{:>+11.2f}%\n",
                   "基准(池内等权)", bench_stats.total_ret * 100.0, bench_stats.annual_ret * 100.0,
                   bench_stats.max_dd * 100.0, bench_stats.sharpe, bench_stats.avg_ret * 100.0);
        fmt::print("  {:<16}{:>+11.2f}%{:>+11.2f}%{:>+11.2f}%{:>10.3f}{:>+11.2f}%\n",
                   "多空(L5-L1)", ls_stats.total_ret * 100.0, ls_stats.annual_ret * 100.0,
                   ls_stats.max_dd * 100.0, ls_stats.sharpe, ls_stats.avg_ret * 100.0);

        // 合成 IC 指标
        std::vector<double> ics;
        for (double x : res.alpha_ic) {
            if (!std::isnan(x)) ics.push_back(x);
        }
        double ic_mean = kNaN, ic_std = kNaN, ic_ir = 0.0, ic_pos = kNaN;
        if (!ics.empty()) {
            ic_mean = std::accumulate(ics.begin(), ics.end(), 0.0) / static_cast<double>(ics.size());
            size_t pos = 0;
            for (double x : ics) {
                if (x > 0.0) ++pos;
            }
            ic_pos = static_cast<double>(pos) / static_cast<double>(ics.size());
            if (ics.size() > 1) {
                double ss = 0.0;
                for (double x : ics) ss += (x - ic_mean) * (x - ic_mean);
                ic_std = std::sqrt(ss / static_cast<double>(ics.size() - 1));
                ic_ir = ic_std > 0.0 ? ic_mean / ic_std : 0.0;  // 与 Python metrics 一致
            }
        }
        fmt::print("\n[合成 IC] 均值={:+.4f}  std={:.4f}  IR={:+.3f}  IC 正比例={:.1f}%  ({} 期)\n",
                   ic_mean, ic_std, ic_ir, ic_pos * 100.0, ics.size());

        print_single_factor_ic(res.factor_ics);

        // Top-N 集中度
        fmt::print("\n[Top-N 集中度] (按 alpha 排名前 N 等权, 超额 vs 穷人版基准)\n");
        fmt::print("  {:<8}{:>12}{:>12}{:>12}{:>14}\n", "组合", "累计收益", "每期均值", "最大回撤", "超额(vs基准)");
        std::map<std::string, SeriesStats> topn_stats;
        for (int k : {5, 10, 20}) {
            std::string key = fmt::format("Top{}", k);
            std::vector<double> rets;
            for (size_t i = 0; i < T; ++i) {
                auto it = res.topn_rets[i].find(key);
                if (it != res.topn_rets[i].end() && !std::isnan(it->second)) rets.push_back(it->second);
            }
            auto st = calc_series_stats(rets, ppy);
            topn_stats[key] = st;
            fmt::print("  {:<8}{:>+11.2f}%{:>+11.2f}%{:>+11.2f}%{:>+13.2f}%\n",
                       key, st.total_ret * 100.0, st.avg_ret * 100.0, st.max_dd * 100.0,
                       (st.total_ret - bench_stats.total_ret) * 100.0);
        }
        fmt::print("  基准口径: 池内 {} 只股票等权下期收益 (Python 原版为沪深 300 指数, 口径不同)\n",
                   panel.size());

        // 一句话洞察
        double l5_l1_nav = layer_stats[4].final_nav - layer_stats[0].final_nav;
        fmt::print("\n[一句话洞察]\n");
        if (l5_l1_nav > 0.0 && ic_ir > 0.2) {
            fmt::print("  分层单调性成立且 IC 稳定 (IR={:+.3f}): L5-L1 净值差 {:+.2f}, 因子组合有效.\n",
                       ic_ir, l5_l1_nav);
        } else if (l5_l1_nav > 0.0) {
            fmt::print("  L5 跑赢 L1 (净值差 {:+.2f}) 但 IC 稳定性一般 (IR={:+.3f}), 因子弱有效.\n",
                       l5_l1_nav, ic_ir);
        } else {
            fmt::print("  L5-L1 净值差 {:+.2f} (倒挂), 等权合成可能被反向因子拖累, 可试 --weight ic.\n",
                       l5_l1_nav);
        }

        // 写文件
        const std::string nav_csv = "outputs/week11/factor_layer_nav.csv";
        const std::string ic_csv = "outputs/week11/factor_ic.csv";
        const std::string json_path = "outputs/week11/factor_backtest_summary.json";
        const std::string png_path = "outputs/week11/factor_layer_nav.png";
        write_nav_csv(nav_csv, res, layer_navs, bench_nav, ls_nav);
        write_ic_csv(ic_csv, res);

        json j;
        j["config"] = {
            {"pool", args.pool}, {"max_stocks", args.max_stocks},
            {"lookback", args.lookback}, {"rebal", args.rebal},
            {"ic_lookback", args.ic_lookback},
            {"weight_method", args.weight == "ic" ? "ic_weighted" : "equal"},
            {"start", args.start}, {"end", args.end},
            {"stocks_loaded", panel.size()},
            {"calendar", {calendar.front(), calendar.back()}},
        };
        j["metrics"] = {
            {"rebal_count", T},
            {"ic_mean", num_or_null(ic_mean)},
            {"ic_std", num_or_null(ic_std)},
            {"ic_ir", round4(ic_ir)},
            {"ic_positive_ratio", num_or_null(ic_pos)},
            {"long_short_total_ret", round4(ls_stats.total_ret)},
            {"long_short_avg_ret", round4(ls_stats.avg_ret)},
            {"L5_total_ret", round4(layer_stats[4].total_ret)},
            {"L1_total_ret", round4(layer_stats[0].total_ret)},
            {"L5_minus_L1_total", round4(l5_l1_nav)},
            {"bench_total_ret", round4(bench_stats.total_ret)},
            {"bench_avg_ret", round4(bench_stats.avg_ret)},
        };
        for (int k : {5, 10, 20}) {
            std::string key = fmt::format("Top{}", k);
            const auto& st = topn_stats[key];
            j["metrics"][key + "_total_ret"] = round4(st.total_ret);
            j["metrics"][key + "_avg_ret"] = round4(st.avg_ret);
            j["metrics"][key + "_max_dd"] = round4(st.max_dd);
            j["metrics"][key + "_excess_vs_bench"] = round4(st.total_ret - bench_stats.total_ret);
        }
        auto stats_json = [](const SeriesStats& st) {
            return json{{"total_ret", round4(st.total_ret)},
                        {"annual_ret", round4(st.annual_ret)},
                        {"max_dd", round4(st.max_dd)},
                        {"sharpe", num_or_null(st.sharpe)},
                        {"avg_ret", round4(st.avg_ret)}};
        };
        json layer_json;
        for (size_t l = 0; l < 5; ++l) layer_json[fmt::format("L{}", l + 1)] = stats_json(layer_stats[l]);
        layer_json["benchmark"] = stats_json(bench_stats);
        layer_json["long_short"] = stats_json(ls_stats);
        j["layer_stats"] = layer_json;
        json single_ic_json;
        for (const auto& fname : quant::factor::factor_names()) {
            std::vector<double> fics;
            for (const auto& m : res.factor_ics) {
                auto it = m.find(fname);
                if (it != m.end() && !std::isnan(it->second)) fics.push_back(it->second);
            }
            if (fics.empty()) continue;
            double mean = std::accumulate(fics.begin(), fics.end(), 0.0) /
                          static_cast<double>(fics.size());
            double sd = 0.0;
            if (fics.size() > 1) {
                double ss = 0.0;
                for (double x : fics) ss += (x - mean) * (x - mean);
                sd = std::sqrt(ss / static_cast<double>(fics.size() - 1));
            }
            size_t pos = 0;
            for (double x : fics) {
                if (x > 0.0) ++pos;
            }
            single_ic_json[fname] = {
                {"ic_mean", round4(mean)}, {"ic_std", round4(sd)},
                {"ic_ir", sd > 0.0 ? round4(mean / sd) : 0.0},
                {"ic_positive_ratio", round4(static_cast<double>(pos) / static_cast<double>(fics.size()))},
                {"samples", fics.size()},
            };
        }
        j["single_factor_ic"] = single_ic_json;
        j["notes"] = {
            "基准为池内股票等权下期收益(穷人版基准), Python 原版为沪深 300 指数, 口径不同",
            "价格为 MySQL 未复权日线, Python 用 xtdata 后复权, 除权附近收益有轻微失真",
            "行业用 trade_stock_status.sector_1; 无行业映射的股票在行业中性化后因子为 NaN",
            "TURN_20 优先用 turnover_rate 真实换手率, 缺失时退回 20/60 日均量相对代理",
            "Z-score 用总体 std(ddof=0), Python 为 ddof=1, 对秩 IC 与等频分层无实质影响",
            "按日历日期对齐处理停牌; Python 假定 xtdata 面板已按 iloc 对齐",
        };
        {
            std::ofstream ofs(json_path);
            if (!ofs.is_open()) throw std::runtime_error(fmt::format("无法写入: {}", json_path));
            ofs << j.dump(2);
        }
        save_layer_chart(png_path, res, layer_navs, bench_nav, ls_nav, weight_label);

        fmt::print("\n[文件输出]\n");
        fmt::print("  净值 CSV : {}\n", nav_csv);
        fmt::print("  IC CSV   : {}\n", ic_csv);
        fmt::print("  摘要 JSON: {}\n", json_path);
        fmt::print("  净值图   : {}\n", png_path);
        fmt::print("\n[结果] {{\"status\": \"success\", \"periods\": {}, \"L5_L1\": \"{:+.2f}%\", \"IC_IR\": \"{:+.3f}\"}}\n",
                   T, l5_l1_nav * 100.0, ic_ir);
        return 0;
    } catch (const std::exception& e) {
        fmt::print(stderr, "[错误] {}\n", e.what());
        return 1;
    }
}
