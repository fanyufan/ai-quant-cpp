# week4(cpp) — Python 到 C++ 的转换

本目录对应 `week4/8-海龟交易法则-20260311/` 下的 Python 案例。Week4 的核心是 **海龟交易法则** 及其变种，因此我们在 `common/` 中扩展了回测绩效、绘图、周线重采样、海龟工具函数等公共能力。

> 说明：`week4/7-OpenClaw-20260307/` 下的脚本按用户要求本次不做 C++ 转换；其中依赖 `xtquant`（MiniQMT）的脚本本来就无法 C++ 化。

## 已转换文件

### 8-海龟交易法则（`8-Turtle_Trading-20260311/`）

| 原 Python | C++ 文件 | CMake target | 策略说明 |
|---|---|---|---|
| `1-经典海龟策略.py` | `01_classic_turtle.cpp` | `w4_turtle_01_classic` | 简单海龟 vs 完整海龟（ATR仓位+金字塔加仓），多标的对比 |
| `2-ADX海龟策略.py` | `02_adx_turtle.cpp` | `w4_turtle_02_adx` | ADX > 15 入场过滤海龟 |
| `3-多周期海龟策略.py` | `03_multi_tf_turtle.cpp` | `w4_turtle_03_multi_tf` | 周线趋势过滤 + 日线海龟入场 |
| `4-ML增强海龟策略.py` | `04_ml_turtle.cpp` | `w4_turtle_04_ml` | 8 特征工程 + C++ 决策树过滤假突破 |

### 公共库扩展

- `common/env.hpp/cpp`：`.env` 解析器（保留原有 `load`/`get` API，新增 `find_and_load_dotenv`）。
- `common/backtest_config.hpp/cpp`：从 `.env` 读取回测参数（`BACKTEST_*`）。
- `common/backtest_report.hpp/cpp`：完整绩效指标（年化、夏普、卡玛、胜率、盈亏比、利润因子、最大连亏等）。
- `common/backtest_plot.hpp/cpp`：三子图回测可视化（价格+买卖点、净值 vs 基准、回撤）。
- `common/weekly_bars.hpp/cpp`：日线按日历周重采样为周线。
- `common/turtle_utils.hpp/cpp`：唐奇安通道滚动高低点、ATR 仓位计算。
- `common/backtest_data_mysql.hpp/cpp`：扩展 MySQL 代码列表、标的名称、年度数据天数查询。

> 注意：`4-ML增强海龟策略.py` 原依赖 `sklearn`/`lightgbm`/`xgboost`。为避免在 `x64-mingw-dynamic` 上引入构建风险较大的第三方库，C++ 版本使用内部实现的浅层 CART 决策树，保留相同的特征设计与时间分割逻辑，但模型引擎与 Python 不同。

## 构建

在项目根目录执行：

```powershell
cd C:\Fan\ai-quant-cpp
cmake -B build -S . -G Ninja
cmake --build build
```

week4 目标名：

- `w4_turtle_01_classic`
- `w4_turtle_02_adx`
- `w4_turtle_03_multi_tf`
- `w4_turtle_04_ml`

## 运行

**必须在项目根目录运行**，因为程序使用相对路径 `outputs/`。

```powershell
# 默认从 MySQL trade_stock_daily 加载数据
./build/bin/Debug/01_classic_turtle.exe
./build/bin/Debug/02_adx_turtle.exe
./build/bin/Debug/03_multi_tf_turtle.exe
./build/bin/Debug/04_ml_turtle.exe

# 使用本地 CSV（方便测试）
./build/bin/Debug/01_classic_turtle.exe --stock 600519.SH --data-file data/600519_SH_daily.csv --start 2024-01-01 --end 2024-12-31
```

通用参数：

| 参数 | 说明 | 默认值 |
|---|---|---|
| `--stock` / `-s` | 股票代码（用于报告） | 各程序不同 |
| `--start` | 回测开始日期 | `2024-01-01` |
| `--end` | 回测结束日期 | `2025-12-31` |
| `--data-file` | 日线 CSV 文件路径（使用 CSV 时不必连 MySQL） | 空（使用 MySQL） |
| `--split` | ML 训练/测试分割日期 | `2025-01-01` |
| `--threshold` | ML 海龟入场概率阈值 | `0.5` |

输出文件：

- `outputs/<label>.png`：回测可视化图表。

## 依赖数据

- 回测默认使用 MySQL `wucai_trade.trade_stock_daily` 表。
- 可用 `week2(cpp)/08_market_data_collection.exe --write-mysql` 灌入数据。
- 测试时也可指定本地 CSV，列名为 `date,open,high,low,close,volume`。
