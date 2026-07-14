#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace quant::tushare {

using json = nlohmann::json;

class Client {
public:
    explicit Client(const std::string& token);

    json daily(const std::string& ts_code,
               const std::string& start_date,
               const std::string& end_date);

    json pro_bar(const std::string& ts_code,
                 const std::string& start_date,
                 const std::string& end_date,
                 const std::string& adj = "qfq",
                 const std::string& freq = "D",
                 const std::string& fields = "");

    json adj_factor(const std::string& ts_code,
                    const std::string& start_date,
                    const std::string& end_date,
                    const std::string& fields = "");

    json stk_mins(const std::string& ts_code,
                  const std::string& freq,
                  const std::string& start_time,
                  const std::string& end_time,
                  const std::string& fields = "");

    json stock_basic(const std::string& exchange = "",
                     const std::string& list_status = "L",
                     const std::string& fields = "");

    json fina_indicator(const std::string& ts_code,
                        const std::string& period,
                        const std::string& fields = "");

    // 宏观经济月频接口
    json cn_cpi(const std::string& start_m,
                const std::string& end_m,
                const std::string& fields = "");

    json cn_ppi(const std::string& start_m,
                const std::string& end_m,
                const std::string& fields = "");

    json cn_pmi(const std::string& start_m,
                const std::string& end_m,
                const std::string& fields = "");

    json cn_m(const std::string& start_m,
              const std::string& end_m,
              const std::string& fields = "");

    json sf_month(const std::string& start_m,
                  const std::string& end_m,
                  const std::string& fields = "");

    json cn_gdp(const std::string& start_q,
                const std::string& end_q,
                const std::string& fields = "");

    json lpr_data(const std::string& start_date,
                  const std::string& end_date,
                  const std::string& fields = "");

    json daily_basic(const std::string& trade_date,
                     const std::string& fields = "");

    json cashflow_vip(const std::string& period,
                      const std::string& fields = "");

    json income_vip(const std::string& period,
                    const std::string& fields = "");

private:
    json call_api(const std::string& api_name, const json& params, const std::string& fields);

    std::string token_;
    std::string base_url_ = "http://api.tushare.pro";
    double last_request_time_ = 0.0;
    static constexpr double REQUEST_INTERVAL = 0.12; // seconds
};

} // namespace quant::tushare
