#pragma once

#include <string>
#include <vector>

namespace quant::date {

struct Date {
    int year = 0;
    int month = 0;
    int day = 0;

    Date() = default;
    Date(int y, int m, int d) : year(y), month(m), day(d) {}
    explicit Date(const std::string& s); // yyyy-mm-dd or yyyy/mm/dd or yyyymmdd

    bool operator==(const Date& o) const;
    bool operator!=(const Date& o) const;
    bool operator<(const Date& o) const;
    bool operator<=(const Date& o) const;
    bool operator>(const Date& o) const;
    bool operator>=(const Date& o) const;

    std::string to_string(const std::string& sep = "-") const;
    std::string to_yyyymmdd() const;
    bool valid() const;
};

Date parse_date(const std::string& s);
std::string format_yyyymmdd(const std::string& s);
std::string latest_report_period();
std::vector<std::string> report_periods_annual(int n = 3);

} // namespace quant::date
