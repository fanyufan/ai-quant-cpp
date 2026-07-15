#include "chan_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <matplot/matplot.h>
#include <unordered_map>
#include <vector>

namespace quant::chan {

namespace {

using namespace matplot;

const char* signal_type_name(SignalType t) {
    switch (t) {
        case SignalType::FirstBuy: return "一买";
        case SignalType::SecondBuy: return "二买";
        case SignalType::ThirdBuy: return "三买";
        case SignalType::ThirdSell: return "三卖";
    }
    return "";
}


std::unordered_map<std::string, int> build_date_to_x(const std::vector<quant::bt::Bar>& bars) {
    std::unordered_map<std::string, int> m;
    for (size_t i = 0; i < bars.size(); ++i) {
        m[bars[i].date] = static_cast<int>(i);
    }
    return m;
}

std::unordered_map<std::string, int> build_date_to_x(const std::vector<ChanBar>& bars) {
    std::unordered_map<std::string, int> m;
    for (size_t i = 0; i < bars.size(); ++i) {
        m[bars[i].date] = static_cast<int>(i);
    }
    return m;
}

int lookup_x(const std::unordered_map<std::string, int>& m, const std::string& date) {
    auto it = m.find(date);
    return (it == m.end()) ? -1 : it->second;
}

std::vector<std::string> make_tick_labels(const std::vector<std::string>& dates,
                                          const std::vector<double>& positions) {
    std::vector<std::string> out;
    out.reserve(positions.size());
    for (double p : positions) {
        int ip = static_cast<int>(p);
        if (ip < 0 || static_cast<size_t>(ip) >= dates.size()) {
            out.emplace_back("");
        } else {
            const std::string& d = dates[ip];
            if (d.size() >= 7) {
                out.emplace_back(d.substr(0, 7));  // YYYY-MM
            } else {
                out.emplace_back(d);
            }
        }
    }
    return out;
}

void draw_candlestick(axes_handle ax,
                      const std::vector<std::string>& dates,
                      const std::vector<double>& open,
                      const std::vector<double>& high,
                      const std::vector<double>& low,
                      const std::vector<double>& close,
                      const std::vector<double>& volume) {
    if (dates.empty()) return;
    size_t n = dates.size();
    auto xs = linspace(0.0, static_cast<double>(n - 1), n);
    ax->hold(on);
    double price_range = *std::max_element(high.begin(), high.end()) -
                         *std::min_element(low.begin(), low.end());
    double min_body = price_range * 0.002;
    for (size_t i = 0; i < n; ++i) {
        double x = xs[i];
        double o = open[i], h = high[i], l = low[i], c = close[i];
        bool up = c >= o;
        std::string color = up ? "red" : "green";
        double body_bottom = up ? o : c;
        double body_height = std::max(std::abs(c - o), min_body);
        ax->rectangle(x - 0.3, body_bottom, 0.6, body_height)->color(color);
        ax->plot({x, x}, {std::max(o, c), h}, color);
        ax->plot({x, x}, {std::min(o, c), l}, color);
    }
    int step = std::max(1, static_cast<int>(n) / 12);
    std::vector<double> ticks;
    for (int i = 0; static_cast<size_t>(i) < n; i += step) ticks.push_back(static_cast<double>(i));
    if (ticks.empty() || static_cast<int>(ticks.back()) != static_cast<int>(n) - 1) {
        ticks.push_back(static_cast<double>(n) - 1);
    }
    ax->xticks(ticks);
    ax->xticklabels(make_tick_labels(dates, ticks));
    ax->xlim({-0.5, static_cast<double>(n) - 0.5});
    ax->grid(on);
}

} // namespace

void ChanAnalyzer::plot(const std::string& save_path,
                        const std::string& title,
                        bool show_bi,
                        bool show_zhongshu,
                        bool show_signals,
                        bool show_fractals,
                        bool show_all_fractals) const {
    if (raw_bars_.empty()) return;

    auto d2x = build_date_to_x(raw_bars_);
    size_t n = raw_bars_.size();

    std::vector<double> opens, highs, lows, closes, volumes;
    opens.reserve(n); highs.reserve(n); lows.reserve(n); closes.reserve(n); volumes.reserve(n);
    for (const auto& b : raw_bars_) {
        opens.push_back(b.open);
        highs.push_back(b.high);
        lows.push_back(b.low);
        closes.push_back(b.close);
        volumes.push_back(b.volume);
    }

    auto fig = figure(false);
    fig->size(1600, 1000);
    fig->title(title);

    std::vector<std::string> dates;
    dates.reserve(n);
    for (const auto& b : raw_bars_) dates.push_back(b.date);

    auto ax1 = subplot(2, 1, 0);
    draw_candlestick(ax1, dates, opens, highs, lows, closes, volumes);
    int step = std::max(1, static_cast<int>(n) / 12);
    std::vector<double> ticks;
    for (int i = 0; static_cast<size_t>(i) < n; i += step) ticks.push_back(static_cast<double>(i));
    if (ticks.empty() || static_cast<int>(ticks.back()) != static_cast<int>(n) - 1) {
        ticks.push_back(static_cast<double>(n) - 1);
    }
    ax1->xticks(ticks);
    ax1->xticklabels(make_tick_labels(dates, ticks));

    const auto& frac_list = show_all_fractals ? fractals_ : confirmed_fractals_;
    if (show_fractals) {
        for (const auto& f : frac_list) {
            int x = lookup_x(d2x, f.raw_date);
            if (x < 0) continue;
            std::string color = (f.type == FractalType::Top) ? "red" : "green";
            std::string marker = (f.type == FractalType::Top) ? "v" : "^";
            ax1->scatter(std::vector<double>{static_cast<double>(x)},
                         std::vector<double>{f.price})
                ->marker(marker).color(color);
        }
    }

    if (show_bi && !bi_list_.empty()) {
        for (const auto& bi : bi_list_) {
            int sx = lookup_x(d2x, bi.start_raw_date);
            int ex = lookup_x(d2x, bi.end_raw_date);
            if (sx < 0 || ex < 0) continue;
            std::string color = bi.up ? "red" : "green";
            ax1->plot({static_cast<double>(sx), static_cast<double>(ex)},
                      {bi.start_price, bi.end_price})
                ->color(color).line_width(1.5);
        }
    }

    if (show_zhongshu && !zhongshu_list_.empty()) {
        for (const auto& zs : zhongshu_list_) {
            int xl = lookup_x(d2x, zs.start_date);
            int xr = lookup_x(d2x, zs.end_date);
            if (xl < 0 || xr < 0) continue;
            ax1->rectangle(static_cast<double>(xl), zs.zd,
                           static_cast<double>(xr - xl), zs.zg - zs.zd)
                ->color("blue").line_width(1.0);
            ax1->text(static_cast<double>(xl), zs.zg,
                      fmt::format(" ZG={:.1f}\n ZD={:.1f}", zs.zg, zs.zd))
                ->font_size(7);
        }
    }

    if (show_signals && !signals_.empty()) {
        for (const auto& s : signals_) {
            int x = lookup_x(d2x, s.date);
            if (x < 0) continue;
            std::string marker = (s.type == SignalType::ThirdSell) ? "v" : "^";
            std::string color;
            switch (s.type) {
                case SignalType::FirstBuy: color = "purple"; break;
                case SignalType::SecondBuy: color = "orange"; break;
                case SignalType::ThirdBuy: color = "red"; break;
                case SignalType::ThirdSell: color = "green"; break;
            }
            ax1->scatter(std::vector<double>{static_cast<double>(x)},
                         std::vector<double>{s.price})
                ->marker(marker).color(color).marker_size(12);
            ax1->text(static_cast<double>(x), s.price, signal_type_name(s.type))
                ->color(color).font_size(8)
                .alignment(labels::alignment::left);
        }
    }

    ax1->ylabel("Price");
    ax1->grid(on);

    auto ax2 = subplot(2, 1, 1);
    auto xs = linspace(0.0, static_cast<double>(n - 1), n);
    ax2->bar(xs, volumes)->face_color("red");
    ax2->xticks(ticks);
    ax2->xticklabels(make_tick_labels(dates, ticks));
    ax2->xlim({-0.5, static_cast<double>(n) - 0.5});
    ax2->ylabel("Volume");
    ax2->grid(on);

    fig->save(save_path);
    fmt::print("  图表已保存: {}\n", save_path);
}

void ChanAnalyzer::plot_compare_merge(const std::string& save_path,
                                      const std::string& title) const {
    (void)title;
    if (raw_bars_.empty() || merged_.empty()) return;

    auto fig = figure(false);
    fig->size(1800, 800);

    // Left: raw bars with pseudo fractals.
    {
        auto ax1 = subplot(1, 2, 0);
        std::vector<std::string> dates;
        std::vector<double> o, h, l, c, v;
        dates.reserve(raw_bars_.size());
        for (const auto& b : raw_bars_) {
            dates.push_back(b.date);
            o.push_back(b.open); h.push_back(b.high); l.push_back(b.low);
            c.push_back(b.close); v.push_back(b.volume);
        }
        draw_candlestick(ax1, dates, o, h, l, c, v);
        for (size_t i = 1; i + 1 < raw_bars_.size(); ++i) {
            const auto& p = raw_bars_[i - 1];
            const auto& cur = raw_bars_[i];
            const auto& n = raw_bars_[i + 1];
            if (cur.high > p.high && cur.high > n.high && cur.low > p.low && cur.low > n.low) {
                ax1->scatter(std::vector<double>{static_cast<double>(i)},
                             std::vector<double>{cur.high})
                    ->marker("v").color("red");
            } else if (cur.low < p.low && cur.low < n.low && cur.high < p.high && cur.high < n.high) {
                ax1->scatter(std::vector<double>{static_cast<double>(i)},
                             std::vector<double>{cur.low})
                    ->marker("^").color("green");
            }
        }
        ax1->title(fmt::format("合并前 (原始{}根K线)", raw_bars_.size()));
    }

    // Right: merged bars with fractals.
    {
        auto ax2 = subplot(1, 2, 1);
        std::vector<std::string> dates;
        std::vector<double> o, h, l, c, v;
        dates.reserve(merged_.size());
        for (const auto& b : merged_) {
            dates.push_back(b.date);
            o.push_back(b.open); h.push_back(b.high); l.push_back(b.low);
            c.push_back(b.close); v.push_back(b.volume);
        }
        draw_candlestick(ax2, dates, o, h, l, c, v);
        auto d2x = build_date_to_x(merged_);
        for (const auto& f : fractals_) {
            int x = lookup_x(d2x, f.date);
            if (x < 0) continue;
            std::string color = (f.type == FractalType::Top) ? "red" : "green";
            std::string marker = (f.type == FractalType::Top) ? "v" : "^";
            ax2->scatter(std::vector<double>{static_cast<double>(x)},
                         std::vector<double>{f.price})
                ->marker(marker).color(color);
        }
        ax2->title(fmt::format("合并后 ({}根K线, 合并{}根)",
                               merged_.size(), raw_bars_.size() - merged_.size()));
    }

    fig->save(save_path);
    fmt::print("  图表已保存: {}\n", save_path);
}

} // namespace quant::chan
