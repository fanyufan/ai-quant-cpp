#pragma once

#include <map>
#include <string>
#include <vector>

#include "backtest.hpp"

namespace quant::chan {

struct ChanBar {
    std::string date;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    std::string high_date;
    std::string low_date;
};

enum class FractalType { Top, Bottom };

struct Fractal {
    size_t idx = 0;
    std::string date;
    std::string raw_date;
    FractalType type = FractalType::Top;
    double price = 0.0;
};

struct Bi {
    size_t start_idx = 0;
    size_t end_idx = 0;
    std::string start_date;
    std::string end_date;
    std::string start_raw_date;
    std::string end_raw_date;
    double start_price = 0.0;
    double end_price = 0.0;
    bool up = true;  // true = up, false = down
};

struct Zhongshu {
    double zg = 0.0;
    double zd = 0.0;
    double center = 0.0;
    size_t start_idx = 0;
    size_t end_idx = 0;
    std::string start_date;
    std::string end_date;
    int bi_count = 0;
};

enum class SignalType { FirstBuy, SecondBuy, ThirdBuy, ThirdSell };

struct Signal {
    SignalType type = SignalType::ThirdBuy;
    std::string date;
    double price = 0.0;
    double zhongshu_zg = 0.0;
    double zhongshu_zd = 0.0;
    double divergence_ratio = 0.0;
};

class ChanAnalyzer {
public:
    explicit ChanAnalyzer(const std::vector<quant::bt::Bar>& raw_bars);

    ChanAnalyzer& analyze(int min_gap = 4, int min_bi = 3, int max_extend = 4);

    const std::vector<quant::bt::Bar>& raw_bars() const { return raw_bars_; }
    const std::vector<ChanBar>& merged_bars() const { return merged_; }
    const std::vector<Fractal>& fractals() const { return fractals_; }
    const std::vector<Fractal>& confirmed_fractals() const { return confirmed_fractals_; }
    const std::vector<Bi>& bi_list() const { return bi_list_; }
    const std::vector<Zhongshu>& zhongshu_list() const { return zhongshu_list_; }
    const std::vector<Signal>& signals() const { return signals_; }
    const std::vector<double>& macd_hist() const { return macd_hist_; }

    // Map signal code by raw date: 1=一买, 2=二买, 3=三买, -3=三卖
    std::map<std::string, int> get_signal_map() const;
    std::map<std::string, double> get_zg_map() const;
    std::map<std::string, double> get_zd_map() const;

    void summary() const;

    void plot(const std::string& save_path,
              const std::string& title = "缠论分析",
              bool show_bi = true,
              bool show_zhongshu = true,
              bool show_signals = true,
              bool show_fractals = true,
              bool show_all_fractals = false) const;

    void plot_compare_merge(const std::string& save_path,
                            const std::string& title = "K线包含合并对比") const;

private:
    std::vector<quant::bt::Bar> raw_bars_;
    std::vector<ChanBar> merged_;
    std::vector<Fractal> fractals_;
    std::vector<Fractal> confirmed_fractals_;
    std::vector<Bi> bi_list_;
    std::vector<Zhongshu> zhongshu_list_;
    std::vector<Signal> signals_;
    std::vector<double> macd_hist_;

    void prepare_macd();
    std::vector<ChanBar> merge_klines() const;
    std::vector<Fractal> identify_fractals() const;
    std::vector<Bi> identify_bi(int min_gap);
    std::vector<Zhongshu> identify_zhongshu(int min_bi, int max_extend) const;
    std::vector<Signal> detect_signals();

    std::vector<Signal> detect_third_buy() const;
    std::vector<Signal> detect_first_buy() const;
    std::vector<Signal> detect_second_buy(const std::vector<Signal>& first_buys) const;
    std::vector<Signal> detect_third_sell() const;

    double calc_macd_area(const std::string& start_date, const std::string& end_date) const;
};

} // namespace quant::chan
