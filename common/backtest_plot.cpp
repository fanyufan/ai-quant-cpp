#include "backtest_plot.hpp"

#include <cmath>
#include <filesystem>
#include <unordered_map>

#include <fmt/format.h>
#include <matplot/matplot.h>

namespace quant::bt {

namespace fs = std::filesystem;
namespace mp = matplot;

void plot_backtest(const Result& result,
                   const std::vector<Bar>& bars,
                   const std::string& stock_code,
                   const std::string& title,
                   const std::string& output_path) {
    if (result.records.empty() || bars.empty()) return;

    fs::create_directories(fs::path(output_path).parent_path());

    // Map date -> bar index for trade markers.
    std::unordered_map<std::string, size_t> date_to_idx;
    for (size_t i = 0; i < bars.size(); ++i) date_to_idx[bars[i].date] = i;

    // Build data series.
    size_t n = result.records.size();
    std::vector<double> xs(n);
    std::iota(xs.begin(), xs.end(), 0.0);

    std::vector<double> close(n), nav_pct(n), benchmark(n), drawdown(n);
    double peak = result.init_cash;
    double close_start = bars.empty() ? 0.0 : bars.front().close;
    for (size_t i = 0; i < n; ++i) {
        close[i] = bars[i].close;
        nav_pct[i] = result.records[i].nav / result.init_cash;
        benchmark[i] = close_start > 0.0 ? bars[i].close / close_start : 1.0;
        if (result.records[i].nav > peak) peak = result.records[i].nav;
        drawdown[i] = peak > 0.0 ? (result.records[i].nav - peak) / peak * 100.0 : 0.0;
    }

    std::vector<double> buy_x, buy_y, sell_x, sell_y;
    for (const auto& t : result.trades) {
        auto it = date_to_idx.find(t.date);
        if (it == date_to_idx.end()) continue;
        if (t.action == "BUY") {
            buy_x.push_back(static_cast<double>(it->second));
            buy_y.push_back(t.price);
        } else if (t.action == "SELL") {
            sell_x.push_back(static_cast<double>(it->second));
            sell_y.push_back(t.price);
        }
    }

    auto fig = mp::figure(false);
    fig->size(1600, 1200);

    // Top: price + trades
    {
        auto ax = mp::subplot(3, 1, 0);
        ax->hold(mp::on);
        ax->plot(xs, close, "-")->color("gray").line_width(1.0);
        if (!buy_x.empty()) {
            ax->scatter(buy_x, buy_y, 8.0)->color("red").marker_face(true).marker("^");
        }
        if (!sell_x.empty()) {
            ax->scatter(sell_x, sell_y, 8.0)->color("green").marker_face(true).marker("v");
        }
        ax->xlim({0.0, static_cast<double>(n - 1)});
        ax->ylabel("Price");
        ax->title(fmt::format("{} {}", title, stock_code));
        ax->grid(mp::on);
        ax->legend({"Close", "Buy", "Sell"});
    }

    // Middle: NAV vs benchmark
    {
        auto ax = mp::subplot(3, 1, 1);
        ax->hold(mp::on);
        ax->plot(xs, nav_pct, "-")->color("steelblue").line_width(1.5);
        ax->plot(xs, benchmark, "-")->color("gray").line_width(1.0);
        ax->plot({0.0, static_cast<double>(n - 1)}, {1.0, 1.0}, "--")->color("red");
        ax->xlim({0.0, static_cast<double>(n - 1)});
        ax->ylabel("Net Value");
        ax->grid(mp::on);
        ax->legend({"Strategy", "Buy & Hold"});
    }

    // Bottom: drawdown
    {
        auto ax = mp::subplot(3, 1, 2);
        ax->hold(mp::on);
        ax->plot(xs, drawdown, "-")->color("darkred").line_width(0.8);
        ax->xlim({0.0, static_cast<double>(n - 1)});
        ax->ylabel("Drawdown (%)");
        ax->xlabel("Trading Days");
        ax->grid(mp::on);
    }

    fig->save(output_path);
}

} // namespace quant::bt
