# week3(cpp) — Python 到 C++ 的转换

本目录对应 `week3/` 下的 Python 案例。Week3 的核心是 **Backtrader 风格的回测** 和 **TA-Lib 技术指标/形态**，因此我们在 `common/` 中实现了一套轻量级 C++ 回测引擎，并手写覆盖了课程所需的指标和常见 K 线形态。

> 说明：原 Python 案例中依赖 `xtquant`（MiniQMT）和 `akshare` 的脚本未做 C++ 转换，因为这两个 SDK 没有 C++ 绑定。行情数据采集请使用 `week2(cpp)/08_market_data_collection.exe`（Tushare → MySQL/CSV）替代。

## 已转换文件

### 5-Backtrader 回测（`5-Backtrader_Backtesting-20260228/`）

| 原 Python | C++ 文件 | 策略说明 |
|---|---|---|
| `5-Backtrader回测/1-双均线策略.py` | `01_dual_ma_strategy.cpp` | 双均线交叉策略（10/30） |
| `5-Backtrader回测/2-MACD策略.py` | `02_macd_strategy.cpp` | MACD 金叉死叉策略 |
| `5-Backtrader回测/3-RSI策略.py` | `03_rsi_strategy.cpp` | RSI 超买超卖策略（30/70） |
| `5-Backtrader回测/4-布林带策略.py` | `04_bollinger_strategy.cpp` | 布林带上下轨策略 |
| `5-Backtrader回测/5-乖离率策略.py` | `05_bias_strategy.cpp` | BIAS 均值回归策略（-6%/+3%） |
| `5-Backtrader回测/6-动量策略.py` | `06_momentum_strategy.cpp` | ROC 动量策略（±5%） |

### 6-Talib 技术指标（`6-Talib_Technical_Indicators-20260304/`）

| 原 Python | C++ 文件 | 说明 |
|---|---|---|
| `6-Talib技术指标库/1-Talib vs Backtrader对比.py` | `01_indicator_comparison.cpp` | 输出 SMA/EMA/MACD/RSI/ATR/BBANDS 等指标最后 5 行 |
| `6-Talib技术指标库/2-Talib基础用法.py` | `02_indicator_basics.cpp` | 打印最新指标数值和最近 5 日 K 线形态 |
| `6-Talib技术指标库/3-K线形态识别.py` | `03_candlestick_patterns.cpp` | 扫描全部 K 线形态并统计看涨/看跌信号 |
| `6-Talib技术指标库/4-RSI策略-优化(穿越确认).py` | `04_rsi_cross_confirm.cpp` | RSI 穿越确认策略（默认确认 2 根 K 线） |
| `6-Talib技术指标库/5-MACD策略-优化(成交量确认).py` | `05_macd_volume_confirm.cpp` | MACD + 成交量均量确认策略 |
| `6-Talib技术指标库/6-MACD策略-优化(利润锁定).py` | `06_macd_trailing_stop.cpp` | MACD + 移动止盈策略 |
| `6-Talib技术指标库/7-布林带策略-优化(中轨止损).py` | `07_bollinger_middle_band_stop.cpp` | 布林带 + 中轨止损策略 |
| `6-Talib技术指标库/8-自适应策略.py` | `08_adaptive_strategy.cpp` | ADX 区分趋势/震荡，切换 MACD/RSI 策略 |
| `6-Talib技术指标库/9-形态选股雷达.py` | `09_pattern_scanner.cpp` | 从 MySQL 加载全市场日线，扫描 MACD 底背离 + 看涨 K 线形态 |

### 未转换文件

- `6-Talib技术指标库/1-行情数据采集.py`：依赖 `xtquant`，可用 `week2(cpp)/08_market_data_collection.exe` 替代。
- `5-Backtrader回测/7-自定义策略.py`：动态加载策略文件，C++ 实现价值不大，保留 Python 版本。
- `6-财经日历采集-20260307.py`：依赖 `akshare`，保留 Python 版本。

## 公共库扩展

- `common/backtest.hpp/cpp`：轻量级回测引擎（Bar、Strategy、Broker、Backtest、Result、CSV/摘要输出）。
- `common/backtest_data.hpp/cpp`：CSV 日线加载与日期过滤。
- `common/backtest_data_mysql.hpp/cpp`：从 MySQL `trade_stock_daily` 加载单股或全量日线。
- `common/indicators.hpp/cpp`：扩展 SMA/EMA/MACD/RSI/ATR/BBANDS/BIAS/ROC/ADX。
- `common/candlestick.hpp/cpp`：常见 K 线形态识别（十字星、锤子线、吞没、孕线、启明星/黄昏星等）。

> 注意：由于 `talib` 在 vcpkg + MinGW 社区 triplet 上构建失败，本实现采用原生 C++ 指标/形态实现，覆盖了课程所需的核心功能，但未逐值对齐 TA-Lib 的 61 种形态。

## 构建

在项目根目录执行：

```powershell
cd C:\Fan\ai-quant-cpp
cmake -B build -S . -G Ninja
cmake --build build
```

week3 目标名前缀规则：

- `w3_bt_xxx`：`5-Backtrader_Backtesting-20260228` 下的回测策略
- `w3_ta_xxx`：`6-Talib_Technical_Indicators-20260304` 下的指标/策略程序

查看所有 week3 目标：

```powershell
ninja -C build -t targets | Select-String -Pattern "^(w3_)"
```

## 运行

**必须在项目根目录运行**，因为程序使用相对路径 `data/` 和 `outputs/`。

### 回测策略（Lesson 5 / 6 的策略类）

```powershell
# 默认使用 data/600519_SH_daily.csv，股票 600519.SH，区间 2025-01-01 ~ 2025-12-31
./build/bin/Debug/01_dual_ma_strategy.exe

# 指定区间（当前 data/600519_SH_daily.csv 实际为 2024 年数据）
./build/bin/Debug/01_dual_ma_strategy.exe --start 2024-01-01 --end 2024-12-31

# 指定其他参数
./build/bin/Debug/04_rsi_cross_confirm.exe --start 2024-01-01 --end 2024-12-31 --confirm 3
./build/bin/Debug/06_macd_trailing_stop.exe --start 2024-01-01 --end 2024-12-31 --trailing-stop 0.06
```

通用参数：

| 参数 | 说明 | 默认值 |
|---|---|---|
| `--stock` | 股票代码（仅用于报告） | `600519.SH` |
| `--start` | 回测开始日期 | `2025-01-01` |
| `--end` | 回测结束日期 | `2025-12-31` |
| `--data-file` | 日线 CSV 文件路径 | `data/600519_SH_daily.csv` |
| `--output-dir` | 输出目录 | `outputs` |

输出文件：

- `outputs/<filename>_nav.csv`：每日净值/收益率/仓位
- `outputs/<filename>_trades.csv`：交易记录

### 指标演示与形态识别（Lesson 6 非回测类）

```powershell
./build/bin/Debug/01_indicator_comparison.exe --start 2024-01-01 --end 2024-12-31
./build/bin/Debug/02_indicator_basics.exe --start 2024-01-01 --end 2024-12-31
./build/bin/Debug/03_candlestick_patterns.exe --start 2024-01-01 --end 2024-12-31
```

### 形态选股雷达（依赖 MySQL）

```powershell
# 使用项目根目录 .env 中的 MySQL 配置
./build/bin/Debug/09_pattern_scanner.exe

# 或命令行覆盖
./build/bin/Debug/09_pattern_scanner.exe --mysql-host localhost --mysql-user quant --mysql-password your_pass --mysql-db wucai_trade

# 限制扫描股票数（测试用）
./build/bin/Debug/09_pattern_scanner.exe --limit 100
```

输出：`outputs/pattern_scanner.csv`

## 依赖数据

- 回测/指标演示：`data/600519_SH_daily.csv` 或其他日线 CSV，列名为 `date,close,open,high,low,volume`。
- 选股雷达：MySQL 数据库中需有 `trade_stock_daily` 表（可用 `week2(cpp)/08_market_data_collection.exe --write-mysql` 灌入）。
