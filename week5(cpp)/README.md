# week5(cpp) — Python 到 C++ 的转换

本目录对应 `week5/` 下的 Python 案例，分为两节课：

- **第 9 课：缠论量化**（`9-Chan_Theory-20260314/`）
- **第 10 课：网格与多因子**（`10-Grid_and_Factors-20260318/`）

Week5 的核心是**自研缠论分析引擎**、**固定/中枢网格引擎**、**多因子计算与选股框架**，以及**C++ 轻量决策树**。为避免引入 `chan.py` 这个外部 Python 库及其依赖，我们选择自研实现缠论核心逻辑；所有直接依赖 `chan.py` 的脚本本次不做 C++ 转换。

> 说明：以下 Python 脚本保持原样，不转换：
> - `9-缠论量化-20260314/CASE-缠论精华量化/1-chan-*`、`2-chan-*`、`3-chan-*`、`4-chan-*`
> - `9-缠论量化-20260314/CASE-缠论精华量化/chanpy_wrapper.py`
> - `10-网格与多因子-20260318/CASE-网格与多因子/2-缠论中枢网格策略.py`
> - `10-网格与多因子-20260318/CASE-网格与多因子/3-中枢网格+趋势联动.py`
> - `10-网格与多因子-20260318/CASE-网格与多因子/7-因子选股+中枢网格.py`
> - `10-网格与多因子-20260318/CASE-网格与多因子/chanpy_wrapper.py`

## 已转换文件

### 9-缠论量化（`9-Chan_Theory-20260314/`）

| 原 Python | C++ 文件 | CMake target | 策略说明 |
|---|---|---|---|
| `1-K线包含处理与分型识别.py` | `01_kline_merge_fractals.cpp` | `w5_chan_01_merge_fractals` | K 线向上/向下包含合并、顶底分型识别、合并前后对比图 |
| `2-笔的自动化识别.py` | `02_bi_recognition.cpp` | `w5_chan_02_bi` | 基于分型序列的贪心笔识别与统计 |
| `3-中枢识别与可视化.py` | `03_zhongshu.cpp` | `w5_chan_03_zhongshu` | 中枢 ZG/ZD、中心、幅度、重叠关系 |
| `4-三类买卖点信号.py` | `04_signals.cpp` | `w5_chan_04_signals` | 一买/二买/三买/三卖信号识别与可视化 |
| `5-缠论三买策略回测.py` | `05_third_buy_backtest.cpp` | `w5_chan_05_third_buy` | 三买触发买入、固定止盈止损的回测 |
| `6-缠论+量价增强策略.py` | `06_volume_enhanced.cpp` | `w5_chan_06_enhanced` | 基础三买 vs 成交量/波动率增强三买 |
| `7-多周期缠论策略.py` | `07_multi_tf.cpp` | `w5_chan_07_multi_tf` | 日线三买信号叠加周线趋势过滤 |
| `8-ML增强缠论策略.py` | `08_ml_chan.cpp` | `w5_chan_08_ml_chan` | 三买样本特征工程 + C++ 决策树过滤 |

### 10-网格与多因子（`10-Grid_and_Factors-20260318/`）

| 原 Python | C++ 文件 | CMake target | 策略说明 |
|---|---|---|---|
| `1-经典网格策略.py` | `01_classic_grid.cpp` | `w5_grid_01_classic` | 固定价格网格，多标的回测对比 |
| `4-多因子评价框架.py` | `04_factor_evaluation.cpp` | `w5_factor_04_eval` | IC、ICIR、五分位分层评价 |
| `5-多因子打分选股.py` | `05_factor_scoring.cpp` | `w5_factor_05_scoring` | 8 因子横截面打分 + Top-N 组合回测 |
| `6-小市值轮动策略.py` | `06_small_cap_rotation.cpp` | `w5_factor_06_small_cap` | 小市值代理 + 动量/波动率过滤的月度轮动 |
| `8-ML增强多因子.py` | `08_ml_multi_factor.cpp` | `w5_factor_08_ml` | 滚动训练 C++ 决策树预测月度收益，选 Top-10 |

### 公共库扩展

- `common/chan_analyzer.hpp/cpp`：自研缠论分析引擎。
  - K 线包含合并（向上/向下）
  - 顶底分型（含确认分型）
  - 贪心笔识别
  - 中枢识别（ZG/ZD、中心、幅度、重叠判断）
  - 一买/二买/三买/三卖信号检测
  - 一买背驰使用 MACD hist 绝对面积比较
- `common/chan_plot.hpp/cpp`：缠论可视化，包括合并对比图、完整分析图（K 线、分型、笔、中枢、信号）。
- `common/grid_engine.hpp/cpp`：固定价格网格引擎；预留以中枢 ZG/ZD 为动态上下界的 `ChanGridEngine`。
- `common/factor_engine.hpp/cpp`：8 技术因子计算（动量、波动率、RSI、ADX、换手率、价格位置、MACD 信号）及横截面打分/选股。
- `common/ml_tree.hpp/cpp`：C++ 轻量 CART 决策树（分类/回归），替代 Python 的 LightGBM/XGBoost/sklearn。

> 注意：`8-ML增强缠论策略.py` 与 `8-ML增强多因子.py` 原依赖 `sklearn`/`lightgbm`/`xgboost`。为避免在 `x64-mingw-dynamic` 上引入构建风险较大的第三方库，C++ 版本使用内部实现的浅层 CART 决策树，保留相同的特征设计与时间分割逻辑，但模型引擎与 Python 不同。

## 构建

在项目根目录执行：

```powershell
cd C:\Fan\ai-quant-cpp
cmake -B build -S . -G Ninja
cmake --build build
```

week5 目标名：

```text
w5_chan_01_merge_fractals
w5_chan_02_bi
w5_chan_03_zhongshu
w5_chan_04_signals
w5_chan_05_third_buy
w5_chan_06_enhanced
w5_chan_07_multi_tf
w5_chan_08_ml_chan
w5_grid_01_classic
w5_factor_04_eval
w5_factor_05_scoring
w5_factor_06_small_cap
w5_factor_08_ml
```

## 运行

**必须在项目根目录运行**，因为程序使用相对路径 `outputs/`。

```powershell
# 缠论分析（默认从 MySQL 加载 600519.SH 数据）
./build/bin/Release/01_kline_merge_fractals.exe
./build/bin/Release/02_bi_recognition.exe
./build/bin/Release/03_zhongshu.exe
./build/bin/Release/04_signals.exe
./build/bin/Release/05_third_buy_backtest.exe
./build/bin/Release/06_volume_enhanced.exe
./build/bin/Release/07_multi_tf.exe
./build/bin/Release/08_ml_chan.exe

# 网格与多因子
./build/bin/Release/01_classic_grid.exe
./build/bin/Release/04_factor_evaluation.exe
./build/bin/Release/05_factor_scoring.exe
./build/bin/Release/06_small_cap_rotation.exe
./build/bin/Release/08_ml_multi_factor.exe
```

输出文件：

- `outputs/1-分型识别_合并对比.png`
- `outputs/1-分型识别_完整图.png`
- `outputs/2-笔识别_*.png`
- `outputs/3-中枢识别_*.png`
- `outputs/4-三类买卖点_*.png`
- `outputs/5-三买策略_*.png`
- `outputs/6-量价增强_*.png`
- `outputs/7-多周期缠论_*.png`
- `outputs/8-ML缠论_*.png`
- `outputs/<标的名称>-网格.png`
- `outputs/4-因子评价_*.png`（如程序中启用保存）
- `outputs/5-因子打分_*.png`
- `outputs/6-小市值轮动_*.png`
- `outputs/8-ML多因子_*.png`

> 提示：图表保存依赖 Matplot++ / gnuplot。在部分终端环境下首次保存可能耗时较长；若长时间无响应，可检查 gnuplot 是否已加入 PATH 并可在命令行直接启动。

## 依赖数据

- 回测默认使用 MySQL `wucai_trade.trade_stock_daily` 表。
- 可用 `week2(cpp)/08_market_data_collection.exe --write-mysql` 灌入数据。
- 多因子评价/选股/小市值轮动/ML 增强程序还会读取 `wucai_trade.trade_stock_financial` 表中的 `total_assets`（总资产）字段；若该表为空，小市值轮动等程序会自动退化使用价格 × 成交量作为市值代理。
- 测试时也可使用本地 CSV，列名为 `date,open,high,low,close,volume`（需根据具体程序支持）。

## 已知限制

1. **缠论实现为教学级简化**：与 `chan.py` 相比，本实现采用更直观的贪心算法，未覆盖递归中枢、线段、多级别联立等高级特性。
2. **一买背驰**：使用 MACD hist 绝对面积近似判断，未严格对齐原文的“MACD 红绿柱面积”。
3. **ML 引擎**：使用自研浅层决策树，特征重要性和预测性能与 Python 的 `lightgbm`/`xgboost` 会有差异。
4. **全市场数据加载**：`04_factor_evaluation.exe`、`05_factor_scoring.exe`、`06_small_cap_rotation.exe`、`08_ml_multi_factor.exe` 会一次性加载所有股票日线，MySQL 数据量较大时首次运行可能需要数十秒到数分钟。
