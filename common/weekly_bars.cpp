#include "weekly_bars.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace quant::bt {

struct YMD {
    int y, m, d;
};

static long jdn(int y, int m, int d) {
    int a = (14 - m) / 12;
    int yy = y + 4800 - a;
    int mm = m + 12 * a - 3;
    long j = d + (153L * mm + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
    return j;
}

static YMD jdn_to_ymd(long j) {
    long l = j + 68569;
    long n = (4 * l) / 146097;
    l = l - (146097 * n + 3) / 4;
    long i = (4000 * (l + 1)) / 1461001;
    l = l - (1461 * i) / 4 + 31;
    long jj = (80 * l) / 2447;
    int d = static_cast<int>(l - (2447 * jj) / 80);
    l = jj / 11;
    int m = static_cast<int>(jj + 2 - 12 * l);
    int y = static_cast<int>(100 * (n - 49) + i + l);
    return {y, m, d};
}

static YMD parse_ymd(const std::string& s) {
    // expect yyyy-mm-dd
    int y = std::stoi(s.substr(0, 4));
    int m = std::stoi(s.substr(5, 2));
    int d = std::stoi(s.substr(8, 2));
    return {y, m, d};
}

static std::string fmt_date(const YMD& ymd) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", ymd.y, ymd.m, ymd.d);
    return buf;
}

std::string week_start(const std::string& date) {
    YMD ymd = parse_ymd(date);
    long jd = jdn(ymd.y, ymd.m, ymd.d);
    int dow = static_cast<int>((jd % 7 + 7) % 7); // 0 = Monday
    jd -= dow;
    return fmt_date(jdn_to_ymd(jd));
}

std::vector<Bar> resample_to_weekly(const std::vector<Bar>& daily) {
    std::vector<Bar> weekly;
    if (daily.empty()) return weekly;

    Bar current;
    std::string current_week;
    bool has_current = false;

    for (const auto& b : daily) {
        std::string ws = week_start(b.date);
        if (!has_current || ws != current_week) {
            if (has_current) weekly.push_back(current);
            current = b;
            current_week = ws;
            has_current = true;
        } else {
            current.high = std::max(current.high, b.high);
            current.low = std::min(current.low, b.low);
            current.close = b.close;
            current.volume += b.volume;
            current.date = b.date; // last trading day of the week
        }
    }
    if (has_current) weekly.push_back(current);
    return weekly;
}

} // namespace quant::bt
