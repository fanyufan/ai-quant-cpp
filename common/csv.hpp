#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <fstream>

namespace quant::csv {

using Row = std::map<std::string, std::string>;
using Table = std::vector<Row>;

struct CsvData {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;

    bool empty() const { return rows.empty(); }
    size_t nrow() const { return rows.size(); }
    size_t ncol() const { return headers.size(); }

    std::optional<size_t> col_index(const std::string& name) const;
    std::vector<std::string> column(const std::string& name) const;
    std::vector<double> column_double(const std::string& name) const;
    std::string get(size_t row, const std::string& name, const std::string& default_value = "") const;
};

CsvData read_csv(const std::string& path);
void write_csv(const std::string& path, const CsvData& data);
void write_csv(const std::string& path,
               const std::vector<std::string>& headers,
               const std::vector<std::vector<std::string>>& rows);

} // namespace quant::csv
