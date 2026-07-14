#pragma once

#include <string>
#include <vector>

namespace quant::bt {

struct Bar {
    std::string date;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
};

struct Trade {
    std::string date;
    std::string action;        // "BUY" or "SELL"
    double price = 0.0;
    int shares = 0;
    double amount = 0.0;
    double commission = 0.0;
    double cash = 0.0;
    double position_ratio = 0.0;
};

struct DailyRecord {
    std::string date;
    double nav = 0.0;
    double cash = 0.0;
    int shares = 0;
    double price = 0.0;
    double position_ratio = 0.0;
    double total_return = 0.0;
};

struct Result {
    std::vector<DailyRecord> records;
    std::vector<Trade> trades;
    double init_cash = 0.0;
    double final_nav = 0.0;
    double total_return = 0.0;
    double max_drawdown = 0.0;
    size_t total_trades = 0;
};

class Broker {
public:
    Broker(double cash, double commission_rate, int lot_size = 100);

    double cash() const { return cash_; }
    int shares() const { return shares_; }
    double nav(double price) const { return cash_ + shares_ * price; }
    double position_ratio(double price) const;
    const std::vector<Trade>& trades() const { return trades_; }

    // target_cash < 0 表示用全部可用现金买入
    void buy(size_t idx, const std::vector<Bar>& bars, double target_cash = -1.0);
    // target_shares < 0 表示卖出全部持仓
    void sell(size_t idx, const std::vector<Bar>& bars, int target_shares = -1);

private:
    double cash_;
    int shares_;
    double commission_rate_;
    int lot_size_;
    std::vector<Trade> trades_;
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual void init(const std::vector<Bar>& bars) { (void)bars; }
    virtual void next(size_t idx, const std::vector<Bar>& bars, Broker& broker) = 0;
};

class Backtest {
public:
    Backtest(double init_cash, double commission_rate, int lot_size = 100);
    Result run(const std::vector<Bar>& bars, Strategy& strategy);

private:
    double init_cash_;
    double commission_rate_;
    int lot_size_;
};

// 输出净值/交易 CSV，文件前缀如 "outputs/dual_ma_strategy"
void write_result_csv(const Result& result, const std::string& prefix);

// 打印与 week1 策略风格一致的回测报告
void print_summary(const Result& result,
                   const std::string& stock_code,
                   const std::string& stock_name,
                   const std::string& start_date,
                   const std::string& end_date);

} // namespace quant::bt
