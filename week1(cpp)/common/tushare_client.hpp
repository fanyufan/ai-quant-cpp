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

    json stock_basic(const std::string& exchange = "",
                     const std::string& list_status = "L",
                     const std::string& fields = "");

    json fina_indicator(const std::string& ts_code,
                        const std::string& period,
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
