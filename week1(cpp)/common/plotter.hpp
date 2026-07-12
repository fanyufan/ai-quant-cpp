#pragma once

#include <string>
#include <vector>
#include <matplot/matplot.h>

namespace quant::plot {

void save_candlestick(const std::string& path,
                      const std::vector<std::string>& dates,
                      const std::vector<double>& open,
                      const std::vector<double>& high,
                      const std::vector<double>& low,
                      const std::vector<double>& close,
                      const std::vector<double>& volume,
                      const std::string& title);

void save_line_chart(const std::string& path,
                     const std::vector<double>& x,
                     const std::vector<double>& y,
                     const std::string& title,
                     const std::string& xlabel,
                     const std::string& ylabel);

void save_multi_subplot(const std::string& path,
                        const std::vector<std::vector<double>>& ys,
                        const std::vector<std::string>& titles,
                        const std::string& main_title);

} // namespace quant::plot
