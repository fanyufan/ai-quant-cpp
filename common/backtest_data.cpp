#include "backtest_data.hpp"

#include <algorithm>
#include <optional>

#include "csv.hpp"
#include "date_utils.hpp"

namespace quant::bt::data {

namespace {

static std::optional<quant::date::Date> parse_date(const std::string& s) {
    if (s.empty()) return std::nullopt;
    quant::date::Date d(s);
    if (d.valid()) return d;
    return std::nullopt;
}

static std::string strip_bom(const std::string& s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        return s.substr(3);
    }
    return s;
}

static std::optional<size_t> find_col(const quant::csv::CsvData& csv, const std::string& name) {
    for (size_t i = 0; i < csv.headers.size(); ++i) {
        if (strip_bom(csv.headers[i]) == name) return i;
    }
    return std::nullopt;
}

static double to_double(const std::string& s) {
    try {
        return std::stod(s);
    } catch (...) {
        return 0.0;
    }
}

} // namespace

std::vector<Bar> load_from_csv(const std::string& path,
                               const std::string& start_date,
                               const std::string& end_date) {
    auto csv = quant::csv::read_csv(path);
    if (csv.empty()) return {};

    auto date_idx = find_col(csv, "date");
    auto open_idx = find_col(csv, "open");
    auto high_idx = find_col(csv, "high");
    auto low_idx = find_col(csv, "low");
    auto close_idx = find_col(csv, "close");
    auto volume_idx = find_col(csv, "volume");

    if (!date_idx || !close_idx) return {};

    std::vector<std::pair<quant::date::Date, Bar>> indexed;
    for (const auto& row : csv.rows) {
        Bar bar;
        bar.date = row[*date_idx];
        if (close_idx) bar.close = to_double(row[*close_idx]);
        if (open_idx) bar.open = to_double(row[*open_idx]);
        if (high_idx) bar.high = to_double(row[*high_idx]);
        if (low_idx) bar.low = to_double(row[*low_idx]);
        if (volume_idx) bar.volume = to_double(row[*volume_idx]);

        quant::date::Date d(bar.date);
        if (!d.valid()) continue;
        indexed.push_back({d, bar});
    }

    std::sort(indexed.begin(), indexed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<Bar> bars;
    bars.reserve(indexed.size());
    for (const auto& [d, bar] : indexed) {
        bars.push_back(bar);
    }
    return filter_by_date(bars, start_date, end_date);
}

std::vector<Bar> filter_by_date(const std::vector<Bar>& bars,
                                const std::string& start_date,
                                const std::string& end_date) {
    auto s = parse_date(start_date);
    auto e = parse_date(end_date);

    std::vector<Bar> out;
    out.reserve(bars.size());
    for (const auto& bar : bars) {
        quant::date::Date d(bar.date);
        if (!d.valid()) continue;
        if (s && d < *s) continue;
        if (e && d > *e) continue;
        out.push_back(bar);
    }
    return out;
}

} // namespace quant::bt::data
