#include "date_utils.hpp"
#include <cctype>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace quant::date {

namespace {

bool is_all_digits(const std::string& s) {
    for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return !s.empty();
}

std::string fmt_yyyymmdd(int y, int m, int d) {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << y
        << std::setw(2) << m
        << std::setw(2) << d;
    return oss.str();
}

} // namespace

Date::Date(const std::string& s) {
    *this = parse_date(s);
}

bool Date::operator==(const Date& o) const {
    return year == o.year && month == o.month && day == o.day;
}
bool Date::operator!=(const Date& o) const { return !(*this == o); }
bool Date::operator<(const Date& o) const {
    if (year != o.year) return year < o.year;
    if (month != o.month) return month < o.month;
    return day < o.day;
}
bool Date::operator<=(const Date& o) const { return *this < o || *this == o; }
bool Date::operator>(const Date& o) const { return !(*this <= o); }
bool Date::operator>=(const Date& o) const { return !(*this < o); }

std::string Date::to_string(const std::string& sep) const {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(4) << year << sep
        << std::setw(2) << month << sep
        << std::setw(2) << day;
    return oss.str();
}

std::string Date::to_yyyymmdd() const {
    return to_string("");
}

bool Date::valid() const {
    if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31) return false;
    return true;
}

Date parse_date(const std::string& s) {
    if (s.empty()) return Date{};

    std::string digits;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
    }

    if (digits.size() == 8 && is_all_digits(digits)) {
        int y = std::stoi(digits.substr(0, 4));
        int m = std::stoi(digits.substr(4, 2));
        int d = std::stoi(digits.substr(6, 2));
        return Date{y, m, d};
    }

    // yyyy-mm-dd or yyyy/mm/dd
    if (s.size() >= 10) {
        try {
            int y = std::stoi(s.substr(0, 4));
            int m = std::stoi(s.substr(5, 2));
            int d = std::stoi(s.substr(8, 2));
            return Date{y, m, d};
        } catch (...) {
        }
    }

    return Date{};
}

std::string format_yyyymmdd(const std::string& s) {
    return parse_date(s).to_yyyymmdd();
}

std::string latest_report_period() {
    auto now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    tm local_tm{};
    localtime_s(&local_tm, &tt);
    int y = local_tm.tm_year + 1900;
    int m = local_tm.tm_mon + 1;

    if (m >= 10) return fmt_yyyymmdd(y, 9, 30);
    if (m >= 8) return fmt_yyyymmdd(y, 6, 30);
    if (m >= 4) return fmt_yyyymmdd(y, 3, 31);
    return fmt_yyyymmdd(y - 1, 9, 30);
}

std::vector<std::string> report_periods_annual(int n) {
    auto now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    tm local_tm{};
    localtime_s(&local_tm, &tt);
    int y = local_tm.tm_year + 1900;
    int m = local_tm.tm_mon + 1;
    if (m < 4) y -= 1;

    std::vector<std::string> result;
    for (int i = 0; i < n; ++i) {
        result.push_back(fmt_yyyymmdd(y - i, 12, 31));
    }
    return result;
}

} // namespace quant::date
