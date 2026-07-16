// common/financial_abstract.hpp
// 财务摘要数据统一加载接口：支持 akshare 风格的 financial_abstract CSV
// 以及 MySQL trade_stock_financial 表。

#ifndef QUANT_FINANCIAL_ABSTRACT_HPP
#define QUANT_FINANCIAL_ABSTRACT_HPP

#include <map>
#include <string>
#include <vector>

// Forward declaration to avoid pulling mysql.h into every translation unit.
namespace quant::mysql {
struct Config;
}

namespace quant::finance {

struct MetricConfig {
    std::string name;        // 中文指标名
    std::string unit;        // 显示单位
    double scale = 1.0;      // 原始值 -> 显示值的除数
    std::string category;    // 指标分类
    bool higher_better = true;
};

struct MetricSeries {
    std::string name;
    std::string unit;
    double scale = 1.0;
    std::string category;
    bool higher_better = true;
    std::vector<std::string> dates; // YYYYMMDD 或 YYYY-MM-DD
    std::vector<double> values;
};

struct FinancialAbstract {
    std::string stock_code;
    std::string stock_name;
    std::map<std::string, MetricSeries> metrics; // key = 中文指标名
};

// 核心指标配置（与 Python ratio_analysis.py / peer_compare.py 对齐）
std::vector<MetricConfig> core_metrics();

// 从 CSV 加载：data_dir/{code}_financial_abstract.csv
FinancialAbstract load_financial_abstract_csv(const std::string& stock_code,
                                              const std::string& data_dir);

// 从 MySQL trade_stock_financial 加载最近 years 个报告期数据。
// MySQL 字段名与中文指标名映射：
//   revenue -> 营业总收入, net_profit -> 净利润, gross_margin -> 毛利率,
//   net_margin -> 销售净利率, roe -> 净资产收益率(ROE), roa -> 总资产报酬率(ROA),
//   debt_ratio -> 资产负债率, eps -> 基本每股收益,
//   operating_cashflow -> 经营现金流量净额
FinancialAbstract load_financial_abstract_mysql(const quant::mysql::Config& cfg,
                                                const std::string& stock_code,
                                                int years = 5);

// 统一加载：优先 CSV；若 CSV 不存在且 use_mysql 为 true，则回退到 MySQL。
FinancialAbstract load_financial_abstract(const std::string& stock_code,
                                          const std::string& data_dir,
                                          bool use_mysql,
                                          const quant::mysql::Config& cfg,
                                          int years = 5);

// 常用名称映射
std::string stock_name_for(const std::string& code);
std::string digits_only(const std::string& code);
std::string to_full_code(const std::string& code);

} // namespace quant::finance

#endif // QUANT_FINANCIAL_ABSTRACT_HPP
