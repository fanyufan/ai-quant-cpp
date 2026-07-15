#include "chan_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "indicators.hpp"

namespace quant::chan {

namespace {

int signal_type_code(SignalType t) {
    switch (t) {
        case SignalType::FirstBuy: return 1;
        case SignalType::SecondBuy: return 2;
        case SignalType::ThirdBuy: return 3;
        case SignalType::ThirdSell: return -3;
    }
    return 0;
}

const char* signal_type_name(SignalType t) {
    switch (t) {
        case SignalType::FirstBuy: return "一买";
        case SignalType::SecondBuy: return "二买";
        case SignalType::ThirdBuy: return "三买";
        case SignalType::ThirdSell: return "三卖";
    }
    return "";
}

std::string signal_type_en(SignalType t) {
    switch (t) {
        case SignalType::FirstBuy: return "first_buy";
        case SignalType::SecondBuy: return "second_buy";
        case SignalType::ThirdBuy: return "third_buy";
        case SignalType::ThirdSell: return "third_sell";
    }
    return "";
}

} // namespace

ChanAnalyzer::ChanAnalyzer(const std::vector<quant::bt::Bar>& raw_bars)
    : raw_bars_(raw_bars) {}

ChanAnalyzer& ChanAnalyzer::analyze(int min_gap, int min_bi, int max_extend) {
    prepare_macd();
    merged_ = merge_klines();
    fractals_ = identify_fractals();
    bi_list_ = identify_bi(min_gap);
    zhongshu_list_ = identify_zhongshu(min_bi, max_extend);
    signals_ = detect_signals();
    return *this;
}

void ChanAnalyzer::prepare_macd() {
    std::vector<double> closes;
    closes.reserve(raw_bars_.size());
    for (const auto& b : raw_bars_) closes.push_back(b.close);
    if (closes.size() >= 35) {
        auto macd = quant::ind::macd(closes, 12, 26, 9);
        macd_hist_ = std::move(macd.bar);
    } else {
        macd_hist_.assign(closes.size(), 0.0);
    }
}

std::vector<ChanBar> ChanAnalyzer::merge_klines() const {
    std::vector<ChanBar> merged;
    merged.reserve(raw_bars_.size());
    for (const auto& row : raw_bars_) {
        ChanBar cb;
        cb.date = row.date;
        cb.open = row.open;
        cb.high = row.high;
        cb.low = row.low;
        cb.close = row.close;
        cb.volume = row.volume;
        cb.high_date = row.date;
        cb.low_date = row.date;

        if (merged.size() < 2) {
            merged.push_back(cb);
            continue;
        }

        ChanBar& prev = merged.back();
        bool inclusion = (cb.high >= prev.high && cb.low <= prev.low) ||
                         (prev.high >= cb.high && prev.low <= cb.low);

        if (!inclusion) {
            merged.push_back(cb);
        } else {
            const ChanBar& prev_prev = merged[merged.size() - 2];
            bool is_up = prev.high >= prev_prev.high;
            if (is_up) {
                double new_high = std::max(prev.high, cb.high);
                double new_low = std::max(prev.low, cb.low);
                std::string h_date = (cb.high >= prev.high) ? cb.high_date : prev.high_date;
                std::string l_date = (cb.low >= prev.low) ? cb.low_date : prev.low_date;
                prev.high = new_high;
                prev.low = new_low;
                prev.close = cb.close;
                prev.volume += cb.volume;
                prev.high_date = h_date;
                prev.low_date = l_date;
            } else {
                double new_high = std::min(prev.high, cb.high);
                double new_low = std::min(prev.low, cb.low);
                std::string h_date = (cb.high <= prev.high) ? cb.high_date : prev.high_date;
                std::string l_date = (cb.low <= prev.low) ? cb.low_date : prev.low_date;
                prev.high = new_high;
                prev.low = new_low;
                prev.close = cb.close;
                prev.volume += cb.volume;
                prev.high_date = h_date;
                prev.low_date = l_date;
            }
        }
    }
    return merged;
}

std::vector<Fractal> ChanAnalyzer::identify_fractals() const {
    std::vector<Fractal> out;
    if (merged_.size() < 3) return out;
    for (size_t i = 1; i + 1 < merged_.size(); ++i) {
        const auto& p = merged_[i - 1];
        const auto& c = merged_[i];
        const auto& n = merged_[i + 1];
        if (c.high > p.high && c.high > n.high && c.low > p.low && c.low > n.low) {
            Fractal f;
            f.idx = i;
            f.date = c.date;
            f.raw_date = c.high_date;
            f.type = FractalType::Top;
            f.price = c.high;
            out.push_back(f);
        } else if (c.low < p.low && c.low < n.low && c.high < p.high && c.high < n.high) {
            Fractal f;
            f.idx = i;
            f.date = c.date;
            f.raw_date = c.low_date;
            f.type = FractalType::Bottom;
            f.price = c.low;
            out.push_back(f);
        }
    }
    return out;
}

std::vector<Bi> ChanAnalyzer::identify_bi(int min_gap) {
    confirmed_fractals_.clear();
    if (fractals_.size() < 2) return {};

    std::vector<Fractal> confirmed = {fractals_[0]};
    for (size_t k = 1; k < fractals_.size(); ++k) {
        const Fractal& f = fractals_[k];
        Fractal& last = confirmed.back();
        if (f.type == last.type) {
            if ((f.type == FractalType::Top && f.price > last.price) ||
                (f.type == FractalType::Bottom && f.price < last.price)) {
                last = f;
            }
        } else {
            if (f.idx - last.idx >= static_cast<size_t>(min_gap)) {
                confirmed.push_back(f);
            }
        }
    }
    confirmed_fractals_ = confirmed;

    std::vector<Bi> out;
    for (size_t i = 1; i < confirmed.size(); ++i) {
        const Fractal& prev = confirmed[i - 1];
        const Fractal& curr = confirmed[i];
        if (prev.type == curr.type) continue;
        Bi b;
        b.start_idx = prev.idx;
        b.end_idx = curr.idx;
        b.start_date = prev.date;
        b.end_date = curr.date;
        b.start_raw_date = prev.raw_date;
        b.end_raw_date = curr.raw_date;
        b.start_price = prev.price;
        b.end_price = curr.price;
        b.up = (prev.type == FractalType::Bottom);
        out.push_back(b);
    }
    return out;
}

std::vector<Zhongshu> ChanAnalyzer::identify_zhongshu(int min_bi, int max_extend) const {
    std::vector<Zhongshu> out;
    if (static_cast<int>(bi_list_.size()) < min_bi) return out;
    size_t i = 0;
    while (i + static_cast<size_t>(min_bi) <= bi_list_.size()) {
        double zg = std::numeric_limits<double>::max();
        double zd = std::numeric_limits<double>::lowest();
        for (int k = 0; k < min_bi; ++k) {
            const Bi& b = bi_list_[i + k];
            double high = std::max(b.start_price, b.end_price);
            double low = std::min(b.start_price, b.end_price);
            zg = std::min(zg, high);
            zd = std::max(zd, low);
        }
        if (zg > zd) {
            size_t end = i + min_bi;
            int extend_count = 0;
            while (end < bi_list_.size() && extend_count < max_extend) {
                const Bi& nb = bi_list_[end];
                double nh = std::max(nb.start_price, nb.end_price);
                double nl = std::min(nb.start_price, nb.end_price);
                if (nh > zd && nl < zg) {
                    ++end;
                    ++extend_count;
                } else {
                    break;
                }
            }
            const Bi& first = bi_list_[i];
            const Bi& last = bi_list_[end - 1];
            Zhongshu zs;
            zs.zg = zg;
            zs.zd = zd;
            zs.center = (zg + zd) / 2.0;
            zs.start_idx = first.start_idx;
            zs.end_idx = last.end_idx;
            zs.start_date = first.start_date;
            zs.end_date = last.end_date;
            zs.bi_count = static_cast<int>(end - i);
            out.push_back(zs);
            i = end;
        } else {
            ++i;
        }
    }
    return out;
}

std::vector<Signal> ChanAnalyzer::detect_signals() {
    std::vector<Signal> out;
    auto third_buys = detect_third_buy();
    auto first_buys = detect_first_buy();
    auto second_buys = detect_second_buy(first_buys);
    auto third_sells = detect_third_sell();
    out.insert(out.end(), third_buys.begin(), third_buys.end());
    out.insert(out.end(), first_buys.begin(), first_buys.end());
    out.insert(out.end(), second_buys.begin(), second_buys.end());
    out.insert(out.end(), third_sells.begin(), third_sells.end());
    std::sort(out.begin(), out.end(), [](const Signal& a, const Signal& b) { return a.date < b.date; });
    return out;
}

std::vector<Signal> ChanAnalyzer::detect_third_buy() const {
    std::vector<Signal> out;
    std::unordered_set<std::string> used_dates;
    for (const auto& zs : zhongshu_list_) {
        double zg = zs.zg;
        double zd = zs.zd;
        enum class State { WaitBreakout, WaitPullback };
        State state = State::WaitBreakout;
        for (const auto& bi : bi_list_) {
            if (bi.start_idx < zs.end_idx) continue;
            if (state == State::WaitBreakout) {
                if (bi.up && bi.end_price > zg) {
                    state = State::WaitPullback;
                }
            } else if (state == State::WaitPullback) {
                if (!bi.up) {
                    if (bi.end_price > zg && used_dates.insert(bi.end_raw_date).second) {
                        Signal s;
                        s.type = SignalType::ThirdBuy;
                        s.date = bi.end_raw_date;
                        s.price = bi.end_price;
                        s.zhongshu_zg = zg;
                        s.zhongshu_zd = zd;
                        out.push_back(s);
                    }
                    break;
                }
            }
        }
    }
    return out;
}

std::vector<Signal> ChanAnalyzer::detect_first_buy() const {
    std::vector<Signal> out;
    if (zhongshu_list_.size() < 2) return out;
    for (size_t j = 1; j < zhongshu_list_.size(); ++j) {
        const auto& prev_zs = zhongshu_list_[j - 1];
        const auto& curr_zs = zhongshu_list_[j];
        if (!(curr_zs.zd < prev_zs.zd && curr_zs.zg < prev_zs.zg)) continue;
        double b_area = calc_macd_area(prev_zs.end_date, curr_zs.start_date);
        const Bi* c_bi = nullptr;
        for (const auto& bi : bi_list_) {
            if (bi.start_idx >= curr_zs.end_idx && !bi.up) {
                c_bi = &bi;
                break;
            }
        }
        if (!c_bi) continue;
        double c_area = calc_macd_area(curr_zs.end_date, c_bi->end_date);
        if (b_area > 0.0 && c_area < b_area * 0.8) {
            Signal s;
            s.type = SignalType::FirstBuy;
            s.date = c_bi->end_raw_date;
            s.price = c_bi->end_price;
            s.zhongshu_zg = curr_zs.zg;
            s.zhongshu_zd = curr_zs.zd;
            s.divergence_ratio = c_area / std::max(b_area, 0.001);
            out.push_back(s);
        }
    }
    return out;
}

std::vector<Signal> ChanAnalyzer::detect_second_buy(const std::vector<Signal>& first_buys) const {
    std::vector<Signal> out;
    for (const auto& fb : first_buys) {
        bool saw_up = false;
        for (const auto& bi : bi_list_) {
            if (bi.start_date <= fb.date) continue;
            if (bi.up) {
                saw_up = true;
            } else if (saw_up) {
                if (bi.end_price > fb.price) {
                    Signal s;
                    s.type = SignalType::SecondBuy;
                    s.date = bi.end_raw_date;
                    s.price = bi.end_price;
                    out.push_back(s);
                }
                break;
            }
        }
    }
    return out;
}

std::vector<Signal> ChanAnalyzer::detect_third_sell() const {
    std::vector<Signal> out;
    std::unordered_set<std::string> used_dates;
    for (const auto& zs : zhongshu_list_) {
        double zg = zs.zg;
        double zd = zs.zd;
        enum class State { WaitBreakdown, WaitBounce };
        State state = State::WaitBreakdown;
        for (const auto& bi : bi_list_) {
            if (bi.start_idx < zs.end_idx) continue;
            if (state == State::WaitBreakdown) {
                if (!bi.up && bi.end_price < zd) {
                    state = State::WaitBounce;
                }
            } else if (state == State::WaitBounce) {
                if (bi.up) {
                    if (bi.end_price < zd && used_dates.insert(bi.end_raw_date).second) {
                        Signal s;
                        s.type = SignalType::ThirdSell;
                        s.date = bi.end_raw_date;
                        s.price = bi.end_price;
                        s.zhongshu_zg = zg;
                        s.zhongshu_zd = zd;
                        out.push_back(s);
                    }
                    break;
                }
            }
        }
    }
    return out;
}

double ChanAnalyzer::calc_macd_area(const std::string& start_date, const std::string& end_date) const {
    double area = 0.0;
    bool in = false;
    for (size_t i = 0; i < raw_bars_.size(); ++i) {
        const auto& d = raw_bars_[i].date;
        if (!in && d >= start_date) in = true;
        if (in) {
            if (!macd_hist_.empty() && i < macd_hist_.size()) {
                area += std::abs(macd_hist_[i]);
            }
            if (d >= end_date) break;
        }
    }
    return area;
}

std::map<std::string, int> ChanAnalyzer::get_signal_map() const {
    std::map<std::string, int> m;
    for (const auto& s : signals_) {
        m[s.date] = signal_type_code(s.type);
    }
    return m;
}

std::map<std::string, double> ChanAnalyzer::get_zg_map() const {
    std::map<std::string, double> m;
    double last = 0.0;
    for (const auto& b : raw_bars_) {
        bool in_zs = false;
        double zg = 0.0;
        for (const auto& zs : zhongshu_list_) {
            if (b.date >= zs.start_date && b.date <= zs.end_date) {
                zg = zs.zg;
                in_zs = true;
                break;
            }
        }
        if (in_zs) last = zg;
        m[b.date] = last;
    }
    return m;
}

std::map<std::string, double> ChanAnalyzer::get_zd_map() const {
    std::map<std::string, double> m;
    double last = 0.0;
    for (const auto& b : raw_bars_) {
        bool in_zs = false;
        double zd = 0.0;
        for (const auto& zs : zhongshu_list_) {
            if (b.date >= zs.start_date && b.date <= zs.end_date) {
                zd = zs.zd;
                in_zs = true;
                break;
            }
        }
        if (in_zs) last = zd;
        m[b.date] = last;
    }
    return m;
}

void ChanAnalyzer::summary() const {
    size_t top_count = 0, bot_count = 0;
    for (const auto& f : fractals_) {
        if (f.type == FractalType::Top) ++top_count;
        else ++bot_count;
    }
    size_t up_count = 0, down_count = 0;
    for (const auto& b : bi_list_) {
        if (b.up) ++up_count;
        else ++down_count;
    }

    fmt::print("{0}\n", std::string(60, '='));
    fmt::print("缠论分析摘要\n");
    fmt::print("{0}\n", std::string(60, '='));
    fmt::print("  原始K线:   {} 根\n", raw_bars_.size());
    fmt::print("  合并后K线: {} 根 (合并了 {} 根)\n",
               merged_.size(), raw_bars_.size() - merged_.size());
    fmt::print("  分型:      {} 个 (顶分型 {}, 底分型 {})\n",
               fractals_.size(), top_count, bot_count);
    fmt::print("  笔:        {} 笔 (上升 {}, 下降 {})\n",
               bi_list_.size(), up_count, down_count);
    fmt::print("  中枢:      {} 个\n", zhongshu_list_.size());

    if (!bi_list_.empty()) {
        double avg_up = 0.0, avg_down = 0.0;
        size_t up_n = 0, down_n = 0;
        for (const auto& b : bi_list_) {
            if (b.up) {
                avg_up += std::abs(b.end_price - b.start_price);
                ++up_n;
            } else {
                avg_down += std::abs(b.end_price - b.start_price);
                ++down_n;
            }
        }
        if (up_n) fmt::print("  上升笔均幅: {:.2f}\n", avg_up / up_n);
        if (down_n) fmt::print("  下降笔均幅: {:.2f}\n", avg_down / down_n);
    }

    if (!zhongshu_list_.empty()) {
        fmt::print("\n  中枢列表:\n");
        for (size_t i = 0; i < zhongshu_list_.size(); ++i) {
            const auto& zs = zhongshu_list_[i];
            fmt::print("    [{}] {} ~ {} | ZG={:.2f} ZD={:.2f} | 包含{}笔\n",
                       i + 1, zs.start_date, zs.end_date, zs.zg, zs.zd, zs.bi_count);
        }
    }

    fmt::print("\n  信号:      {} 个\n", signals_.size());
    for (const auto& s : signals_) {
        std::string extra;
        if (s.type == SignalType::FirstBuy && s.divergence_ratio > 0.0) {
            extra = fmt::format(" | 背驰比={:.2f}", s.divergence_ratio);
        }
        fmt::print("    {} | {} | 价格={:.2f}{}\n",
                   s.date, signal_type_name(s.type), s.price, extra);
    }
    fmt::print("{0}\n", std::string(60, '='));
}

} // namespace quant::chan
