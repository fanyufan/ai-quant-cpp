#include "candlestick.hpp"

#include <cmath>
#include <algorithm>

namespace quant::candle {

namespace {

inline double body(const quant::bt::Bar& b) {
    return std::abs(b.close - b.open);
}

inline double range(const quant::bt::Bar& b) {
    return std::max(b.high - b.low, 1e-9);
}

inline bool is_bull(const quant::bt::Bar& b) { return b.close > b.open; }
inline bool is_bear(const quant::bt::Bar& b) { return b.close < b.open; }

inline double lower_shadow(const quant::bt::Bar& b) {
    return std::min(b.open, b.close) - b.low;
}

inline double upper_shadow(const quant::bt::Bar& b) {
    return b.high - std::max(b.open, b.close);
}

const double EPS = 1e-9;

} // namespace

Signal doji(const quant::bt::Bar& bar) {
    if (range(bar) < EPS) return 0;
    return (body(bar) / range(bar) < 0.1) ? 0 : 0; // 十字星本身中性
}

Signal hammer(const quant::bt::Bar& bar) {
    if (range(bar) < EPS) return 0;
    double ls = lower_shadow(bar);
    double us = upper_shadow(bar);
    double b = body(bar);
    if (ls > 2.0 * b && us < b && ls > range(bar) * 0.5) {
        return 1;
    }
    return 0;
}

Signal hanging_man(const quant::bt::Bar& bar) {
    if (range(bar) < EPS) return 0;
    double ls = lower_shadow(bar);
    double us = upper_shadow(bar);
    double b = body(bar);
    if (ls > 2.0 * b && us < b && ls > range(bar) * 0.5) {
        return -1;
    }
    return 0;
}

Signal inverted_hammer(const quant::bt::Bar& bar) {
    if (range(bar) < EPS) return 0;
    double us = upper_shadow(bar);
    double ls = lower_shadow(bar);
    double b = body(bar);
    if (us > 2.0 * b && ls < b && us > range(bar) * 0.5) {
        return 1;
    }
    return 0;
}

Signal shooting_star(const quant::bt::Bar& bar) {
    if (range(bar) < EPS) return 0;
    double us = upper_shadow(bar);
    double ls = lower_shadow(bar);
    double b = body(bar);
    if (us > 2.0 * b && ls < b && us > range(bar) * 0.5) {
        return -1;
    }
    return 0;
}

Signal bullish_engulfing(const quant::bt::Bar& prev, const quant::bt::Bar& curr) {
    if (is_bear(prev) && is_bull(curr) &&
        curr.open < prev.close && curr.close > prev.open) {
        return 1;
    }
    return 0;
}

Signal bearish_engulfing(const quant::bt::Bar& prev, const quant::bt::Bar& curr) {
    if (is_bull(prev) && is_bear(curr) &&
        curr.open > prev.close && curr.close < prev.open) {
        return -1;
    }
    return 0;
}

Signal bullish_harami(const quant::bt::Bar& prev, const quant::bt::Bar& curr) {
    if (is_bear(prev) && is_bull(curr) &&
        curr.open > prev.close && curr.close < prev.open) {
        return 1;
    }
    return 0;
}

Signal bearish_harami(const quant::bt::Bar& prev, const quant::bt::Bar& curr) {
    if (is_bull(prev) && is_bear(curr) &&
        curr.open < prev.close && curr.close > prev.open) {
        return -1;
    }
    return 0;
}

Signal piercing_pattern(const quant::bt::Bar& prev, const quant::bt::Bar& curr) {
    if (is_bear(prev) && is_bull(curr) &&
        curr.open < prev.low &&
        curr.close > (prev.open + prev.close) / 2.0 &&
        curr.close < prev.open) {
        return 1;
    }
    return 0;
}

Signal dark_cloud_cover(const quant::bt::Bar& prev, const quant::bt::Bar& curr) {
    if (is_bull(prev) && is_bear(curr) &&
        curr.open > prev.high &&
        curr.close < (prev.open + prev.close) / 2.0 &&
        curr.close > prev.open) {
        return -1;
    }
    return 0;
}

Signal morning_star(const quant::bt::Bar& first, const quant::bt::Bar& second, const quant::bt::Bar& third) {
    if (is_bear(first) && body(second) < range(first) * 0.3 && is_bull(third) &&
        third.close > (first.open + first.close) / 2.0) {
        return 1;
    }
    return 0;
}

Signal evening_star(const quant::bt::Bar& first, const quant::bt::Bar& second, const quant::bt::Bar& third) {
    if (is_bull(first) && body(second) < range(first) * 0.3 && is_bear(third) &&
        third.close < (first.open + first.close) / 2.0) {
        return -1;
    }
    return 0;
}

std::vector<std::pair<std::string, Signal>> scan(const std::vector<quant::bt::Bar>& bars, size_t i) {
    std::vector<std::pair<std::string, Signal>> out;
    if (i >= bars.size()) return out;

    auto add = [&](const std::string& name, Signal s) {
        if (s != 0) out.emplace_back(name, s);
    };

    add("doji", doji(bars[i]));
    add("hammer", hammer(bars[i]));
    add("hanging_man", hanging_man(bars[i]));
    add("inverted_hammer", inverted_hammer(bars[i]));
    add("shooting_star", shooting_star(bars[i]));

    if (i >= 1) {
        add("bullish_engulfing", bullish_engulfing(bars[i - 1], bars[i]));
        add("bearish_engulfing", bearish_engulfing(bars[i - 1], bars[i]));
        add("bullish_harami", bullish_harami(bars[i - 1], bars[i]));
        add("bearish_harami", bearish_harami(bars[i - 1], bars[i]));
        add("piercing_pattern", piercing_pattern(bars[i - 1], bars[i]));
        add("dark_cloud_cover", dark_cloud_cover(bars[i - 1], bars[i]));
    }
    if (i >= 2) {
        add("morning_star", morning_star(bars[i - 2], bars[i - 1], bars[i]));
        add("evening_star", evening_star(bars[i - 2], bars[i - 1], bars[i]));
    }
    return out;
}

int composite_signal(const std::vector<quant::bt::Bar>& bars, size_t i) {
    auto patterns = scan(bars, i);
    int score = 0;
    for (const auto& [name, sig] : patterns) {
        score += sig;
    }
    return score;
}

} // namespace quant::candle
