// common/financial_abstract.cpp

#include "financial_abstract.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

#include <fmt/format.h>

#include "backtest_data_mysql.hpp"
#include "csv.hpp"

namespace quant::finance {

namespace {

const double NaN = std::numeric_limits<double>::quiet_NaN();

std::string normalize_date(const std::string& d) {
    if (d.size() == 8 && std::all_of(d.begin(), d.end(), ::isdigit)) {
        return d.substr(0, 4) + "-" + d.substr(4, 2) + "-" + d.substr(6, 2);
    }
    return d;
}

std::string period_key(const std::string& d) {
    // d may be YYYY-MM-DD or YYYYMMDD; return YYYYMMDD.
    std::string s;
    for (char c : d) if (c != '-') s.push_back(c);
    return s;
}

bool is_annual(const std::string& d) {
    std::string p = period_key(d);
    return p.size() == 8 && p.substr(4, 4) == "1231";
}

} // namespace

std::vector<MetricConfig> core_metrics() {
    return {
        {"营业总收入", "亿元", 1e8, "规模", true},
        {"归母净利润", "亿元", 1e8, "规模", true},
        {"净利润", "亿元", 1e8, "规模", true},
        {"毛利率", "%", 1.0, "盈利", true},
        {"销售净利率", "%", 1.0, "盈利", true},
        {"净资产收益率(ROE)", "%", 1.0, "盈利", true},
        {"总资产报酬率(ROA)", "%", 1.0, "盈利", true},
        {"资产负债率", "%", 1.0, "风险", false},
        {"基本每股收益", "元", 1.0, "每股", true},
        {"每股净资产", "元", 1.0, "每股", true},
        {"每股经营现金流", "元", 1.0, "每股", true},
        {"经营现金流量净额", "亿元", 1e8, "现金流", true},
        {"期间费用率", "%", 1.0, "效率", false},
    };
}

std::string stock_name_for(const std::string& code) {
    static const std::map<std::string, std::string> names = {
        {"600519", "贵州茅台"}, {"000858", "五粮液"}, {"688981", "中芯国际"},
        {"002594", "比亚迪"}, {"300750", "宁德时代"}, {"601012", "隆基绿能"},
        {"600036", "招商银行"}, {"601318", "中国平安"}, {"603288", "海天味业"},
        {"600276", "恒瑞医药"},
    };
    std::string digits = code;
    if (code.size() == 9 && code[6] == '.') digits = code.substr(0, 6);
    auto it = names.find(digits);
    return it != names.end() ? it->second : digits;
}

std::string digits_only(const std::string& code) {
    std::string out;
    for (char c : code) if (c >= '0' && c <= '9') out.push_back(c);
    return out;
}

std::string to_full_code(const std::string& code) {
    if (code.size() == 9 && code[6] == '.') return code;
    std::string digits = digits_only(code);
    if (digits.size() != 6) return code;
    if (digits[0] == '0' || digits[0] == '3') return digits + ".SZ";
    return digits + ".SH";
}

FinancialAbstract load_financial_abstract_csv(const std::string& stock_code,
                                              const std::string& data_dir) {
    FinancialAbstract out;
    out.stock_code = digits_only(stock_code);
    out.stock_name = stock_name_for(stock_code);

    std::string path = data_dir;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
    path += out.stock_code + "_financial_abstract.csv";

    auto data = quant::csv::read_csv(path);
    if (data.empty()) return out;

    // Identify date columns: numeric strings, excluding 选项/指标.
    std::vector<std::string> date_cols;
    for (const auto& h : data.headers) {
        if (h == "选项" || h == "指标") continue;
        bool all_digit = !h.empty() && std::all_of(h.begin(), h.end(), ::isdigit);
        if (all_digit) date_cols.push_back(h);
    }
    std::sort(date_cols.begin(), date_cols.end(), std::greater<std::string>());

    auto metric_configs = core_metrics();
    for (const auto& cfg : metric_configs) {
        auto idx = data.col_index("指标");
        if (!idx) continue;
        size_t row_idx = data.nrow();
        for (size_t r = 0; r < data.nrow(); ++r) {
            if (data.rows[r][*idx] == cfg.name) {
                row_idx = r;
                break;
            }
        }
        if (row_idx == data.nrow()) continue;

        MetricSeries s;
        s.name = cfg.name;
        s.unit = cfg.unit;
        s.scale = cfg.scale;
        s.category = cfg.category;
        s.higher_better = cfg.higher_better;
        for (const auto& d : date_cols) {
            auto cidx = data.col_index(d);
            if (!cidx) continue;
            const std::string& val_str = data.rows[row_idx][*cidx];
            if (val_str.empty()) continue;
            try {
                double v = std::stod(val_str);
                s.dates.push_back(normalize_date(d));
                s.values.push_back(v / cfg.scale);
            } catch (...) {
                continue;
            }
        }
        if (!s.dates.empty()) out.metrics[cfg.name] = std::move(s);
    }
    return out;
}

FinancialAbstract load_financial_abstract_mysql(const quant::mysql::Config& cfg,
                                                const std::string& stock_code,
                                                int years) {
    FinancialAbstract out;
    out.stock_code = stock_code;
    out.stock_name = stock_name_for(stock_code);

    // MySQL field -> (Chinese metric name, scale, category, higher_better).
    std::vector<std::tuple<std::string, std::string, double, std::string, bool>> mapping = {
        {"revenue", "营业总收入", 1e8, "规模", true},
        {"net_profit", "净利润", 1e8, "规模", true},
        {"gross_margin", "毛利率", 1.0, "盈利", true},
        {"net_margin", "销售净利率", 1.0, "盈利", true},
        {"roe", "净资产收益率(ROE)", 1.0, "盈利", true},
        {"roa", "总资产报酬率(ROA)", 1.0, "盈利", true},
        {"debt_ratio", "资产负债率", 1.0, "风险", false},
        {"eps", "基本每股收益", 1.0, "每股", true},
        {"operating_cashflow", "经营现金流量净额", 1e8, "现金流", true},
    };

    std::vector<std::string> fields;
    for (const auto& t : mapping) fields.push_back(std::get<0>(t));

    auto raw = quant::bt::data::load_financial_data(cfg, fields);
    auto it = raw.find(stock_code);
    if (it == raw.end()) it = raw.find(to_full_code(stock_code));
    if (it == raw.end()) {
        // Try matching by first 6 digits.
        std::string digits = digits_only(stock_code);
        for (auto mit = raw.begin(); mit != raw.end(); ++mit) {
            if (digits_only(mit->first) == digits) { it = mit; break; }
        }
    }
    if (it == raw.end()) return out;

    // Assemble per-metric series.
    std::map<std::string, MetricSeries> tmp;
    for (const auto& t : mapping) {
        const std::string& field = std::get<0>(t);
        const std::string& cname = std::get<1>(t);
        double scale = std::get<2>(t);
        const std::string& cat = std::get<3>(t);
        bool hb = std::get<4>(t);
        auto f_it = it->second.find(field);
        if (f_it == it->second.end()) continue;
        MetricSeries s;
        s.name = cname;
        s.unit = (scale == 1e8) ? "亿元" : ((field == "eps") ? "元" : "%");
        s.scale = scale;
        s.category = cat;
        s.higher_better = hb;
        for (const auto& rec : f_it->second) {
            s.dates.push_back(rec.date);
            s.values.push_back(rec.value / scale);
        }
        if (!s.dates.empty()) tmp[cname] = std::move(s);
    }

    // Keep only the latest `years` annual report dates (report_date ending 12-31).
    std::set<std::string, std::greater<std::string>> annual_dates;
    for (const auto& kv : tmp) {
        for (size_t i = 0; i < kv.second.dates.size(); ++i) {
            if (is_annual(kv.second.dates[i])) annual_dates.insert(kv.second.dates[i]);
        }
    }
    std::vector<std::string> keep;
    for (const auto& d : annual_dates) {
        keep.push_back(d);
        if (static_cast<int>(keep.size()) >= years) break;
    }
    std::sort(keep.begin(), keep.end(), std::greater<std::string>());

    for (const auto& kv : tmp) {
        MetricSeries s;
        s.name = kv.second.name;
        s.unit = kv.second.unit;
        s.scale = kv.second.scale;
        s.category = kv.second.category;
        s.higher_better = kv.second.higher_better;
        for (size_t i = 0; i < kv.second.dates.size(); ++i) {
            if (std::find(keep.begin(), keep.end(), kv.second.dates[i]) != keep.end()) {
                s.dates.push_back(kv.second.dates[i]);
                s.values.push_back(kv.second.values[i]);
            }
        }
        if (!s.dates.empty()) out.metrics[kv.first] = std::move(s);
    }
    return out;
}

FinancialAbstract load_financial_abstract(const std::string& stock_code,
                                          const std::string& data_dir,
                                          bool use_mysql,
                                          const quant::mysql::Config& cfg,
                                          int years) {
    auto csv = load_financial_abstract_csv(stock_code, data_dir);
    if (!csv.metrics.empty()) return csv;
    if (use_mysql) return load_financial_abstract_mysql(cfg, stock_code, years);
    return csv;
}

} // namespace quant::finance
