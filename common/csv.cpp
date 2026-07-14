#include "csv.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace quant::csv {

static std::string trim_bom(const std::string& s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        return s.substr(3);
    }
    return s;
}

static std::vector<std::string> split_line(const std::string& line) {
    std::vector<std::string> cols;
    std::string cur;
    bool in_quote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            if (in_quote && i + 1 < line.size() && line[i + 1] == '"') {
                cur += '"';
                ++i;
            } else {
                in_quote = !in_quote;
            }
        } else if (c == ',' && !in_quote) {
            cols.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    cols.push_back(cur);
    return cols;
}

CsvData read_csv(const std::string& path) {
    CsvData data;
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return data;
    }

    std::string line;
    if (!std::getline(ifs, line)) {
        return data;
    }
    line = trim_bom(line);
    data.headers = split_line(line);

    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        auto row = split_line(line);
        // Pad if needed
        while (row.size() < data.headers.size()) row.push_back("");
        if (row.size() > data.headers.size()) row.resize(data.headers.size());
        data.rows.push_back(std::move(row));
    }
    return data;
}

void write_csv(const std::string& path, const CsvData& data) {
    write_csv(path, data.headers, data.rows);
}

void write_csv(const std::string& path,
               const std::vector<std::string>& headers,
               const std::vector<std::vector<std::string>>& rows) {
    std::ofstream ofs(path, std::ios::binary);
    // UTF-8 BOM for Excel compatibility
    unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    ofs.write(reinterpret_cast<char*>(bom), 3);

    for (size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) ofs << ',';
        ofs << headers[i];
    }
    ofs << '\n';

    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            if (i > 0) ofs << ',';
            // Simple quoting if comma/quote/newline present
            bool need_quote = row[i].find(',') != std::string::npos ||
                              row[i].find('"') != std::string::npos ||
                              row[i].find('\n') != std::string::npos;
            if (need_quote) {
                ofs << '"';
                for (char c : row[i]) {
                    if (c == '"') ofs << '"';
                    ofs << c;
                }
                ofs << '"';
            } else {
                ofs << row[i];
            }
        }
        ofs << '\n';
    }
}

std::optional<size_t> CsvData::col_index(const std::string& name) const {
    for (size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == name) return i;
    }
    return std::nullopt;
}

std::vector<std::string> CsvData::column(const std::string& name) const {
    std::vector<std::string> result;
    auto idx = col_index(name);
    if (!idx) return result;
    for (const auto& row : rows) {
        result.push_back(row[*idx]);
    }
    return result;
}

std::vector<double> CsvData::column_double(const std::string& name) const {
    std::vector<double> result;
    auto idx = col_index(name);
    if (!idx) return result;
    for (const auto& row : rows) {
        try {
            result.push_back(std::stod(row[*idx]));
        } catch (...) {
            result.push_back(std::numeric_limits<double>::quiet_NaN());
        }
    }
    return result;
}

std::string CsvData::get(size_t row, const std::string& name, const std::string& default_value) const {
    auto idx = col_index(name);
    if (!idx || row >= rows.size()) return default_value;
    return rows[row][*idx];
}

} // namespace quant::csv
