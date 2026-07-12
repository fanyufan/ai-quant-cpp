#include "plotter.hpp"
#include <fmt/format.h>

namespace quant::plot {

using namespace matplot;

void save_candlestick(const std::string& path,
                      const std::vector<std::string>& dates,
                      const std::vector<double>& open,
                      const std::vector<double>& high,
                      const std::vector<double>& low,
                      const std::vector<double>& close,
                      const std::vector<double>& volume,
                      const std::string& title) {
    if (dates.empty() || open.empty()) return;

    using namespace matplot;

    // Create figure with 2 subplots
    auto fig = figure(false);
    fig->size(1400, 800);
    fig->title(title);

    auto ax1 = subplot(2, 1, 0);
    auto xs = linspace(0.0, static_cast<double>(dates.size() - 1), dates.size());
    ax1->hold(on);
    // Draw candlesticks manually
    for (size_t i = 0; i < dates.size(); ++i) {
        double x = xs[i];
        double o = open[i], h = high[i], l = low[i], c = close[i];
        std::string color = (c >= o) ? "red" : "green";
        double body_bottom = (c >= o) ? o : c;
        double body_height = std::abs(c - o);
        if (body_height < 1e-9) body_height = (h - l) * 0.02;

        // body
        ax1->rectangle(x - 0.3, body_bottom, 0.6, body_height)->color(color);
        // wicks
        ax1->plot({x, x}, {std::max(o, c), h}, color);
        ax1->plot({x, x}, {std::min(o, c), l}, color);
    }
    ax1->xlim({-0.5, static_cast<double>(dates.size()) - 0.5});
    ax1->ylabel("Price");
    ax1->grid(on);

    auto ax2 = subplot(2, 1, 1);
    std::vector<double> colors;
    for (size_t i = 0; i < dates.size(); ++i) {
        colors.push_back(close[i] >= open[i] ? 1.0 : 0.0);
    }
    ax2->bar(xs, volume)->face_color("red");
    ax2->ylabel("Volume");
    ax2->xlim({-0.5, static_cast<double>(dates.size()) - 0.5});
    ax2->grid(on);

    fig->save(path);
}

void save_line_chart(const std::string& path,
                     const std::vector<double>& x,
                     const std::vector<double>& y,
                     const std::string& title,
                     const std::string& xlabel,
                     const std::string& ylabel) {
    if (x.empty() || y.empty()) return;

    auto fig = figure(false);
    fig->size(1200, 600);
    auto ax = fig->current_axes();
    ax->plot(x, y)->line_width(1.5);
    ax->xlabel(xlabel);
    ax->ylabel(ylabel);
    ax->title(title);
    ax->grid(on);
    fig->save(path);
}

void save_multi_subplot(const std::string& path,
                        const std::vector<std::vector<double>>& ys,
                        const std::vector<std::string>& titles,
                        const std::string& main_title) {
    if (ys.empty()) return;

    auto fig = figure(false);
    fig->size(1400, 1200);
    fig->title(main_title);

    for (size_t i = 0; i < ys.size(); ++i) {
        auto ax = subplot(static_cast<int>(ys.size()), 1, static_cast<int>(i));
        std::vector<double> xs(ys[i].size());
        for (size_t j = 0; j < ys[i].size(); ++j) xs[j] = static_cast<double>(j);
        ax->plot(xs, ys[i])->line_width(1.2);
        ax->title(titles[i]);
        ax->grid(on);
    }
    fig->save(path);
}

} // namespace quant::plot
