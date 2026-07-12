#include "tushare_client.hpp"
#include <cpr/cpr.h>
#include <fmt/format.h>
#include <thread>
#include <chrono>
#include <stdexcept>

namespace quant::tushare {

Client::Client(const std::string& token) : token_(token) {}

json Client::call_api(const std::string& api_name, const json& params, const std::string& fields) {
    // Rate limiting
    auto now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    double wait = last_request_time_ + REQUEST_INTERVAL - now;
    if (wait > 0) {
        std::this_thread::sleep_for(std::chrono::duration<double>(wait));
    }

    json body;
    body["api_name"] = api_name;
    body["token"] = token_;
    body["params"] = params;
    body["fields"] = fields;

    auto response = cpr::Post(
        cpr::Url{base_url_},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Body{body.dump()}
    );

    last_request_time_ = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    if (response.status_code != 200) {
        throw std::runtime_error(fmt::format("HTTP error {}: {}", response.status_code, response.text));
    }

    json result = json::parse(response.text);
    if (result.contains("code") && result["code"].get<int>() != 0) {
        throw std::runtime_error(fmt::format("Tushare API error: {}", result.value("msg", "unknown")));
    }
    return result;
}

json Client::daily(const std::string& ts_code,
                   const std::string& start_date,
                   const std::string& end_date) {
    json params;
    params["ts_code"] = ts_code;
    params["start_date"] = start_date;
    params["end_date"] = end_date;
    return call_api("daily", params,
                    "ts_code,trade_date,open,high,low,close,vol,amount");
}

json Client::stock_basic(const std::string& exchange,
                         const std::string& list_status,
                         const std::string& fields) {
    json params;
    if (!exchange.empty()) params["exchange"] = exchange;
    if (!list_status.empty()) params["list_status"] = list_status;
    std::string f = fields.empty() ? "ts_code,name,industry,market" : fields;
    return call_api("stock_basic", params, f);
}

json Client::fina_indicator(const std::string& ts_code,
                            const std::string& period,
                            const std::string& fields) {
    json params;
    params["ts_code"] = ts_code;
    params["period"] = period;
    std::string f = fields.empty() ? "ts_code,end_date,roe,bps,eps,debt_to_assets,netprofit_yoy" : fields;
    return call_api("fina_indicator", params, f);
}

json Client::daily_basic(const std::string& trade_date,
                         const std::string& fields) {
    json params;
    params["trade_date"] = trade_date;
    std::string f = fields.empty() ? "ts_code,trade_date,close,pe,pb,total_mv" : fields;
    return call_api("daily_basic", params, f);
}

json Client::cashflow_vip(const std::string& period,
                          const std::string& fields) {
    json params;
    params["period"] = period;
    std::string f = fields.empty() ? "ts_code,end_date,n_cashflow_act,report_type" : fields;
    return call_api("cashflow_vip", params, f);
}

json Client::income_vip(const std::string& period,
                        const std::string& fields) {
    json params;
    params["period"] = period;
    std::string f = fields.empty() ? "ts_code,end_date,n_income,n_income_attr_p,report_type" : fields;
    return call_api("income_vip", params, f);
}

} // namespace quant::tushare
