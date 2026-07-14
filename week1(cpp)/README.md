# week1(cpp) — Python 到 C++ 的转换

本目录对应 `week1/` 下的 Python 案例，使用项目公共库 `common/` 进行了 C++17 重写。

## 已转换文件

| 原 Python 案例 | C++ 文件 | 说明 |
|---|---|---|
| `1-AI 量化交易/1-Tushare数据下载.py` | `1-AI_Quant_Trading-20260204/01_tushare_download_data.cpp` | 下载单只股票日线数据并绘制 K 线/成交量图 |
| `1-AI 量化交易/2-MACD策略.py` | `1-AI_Quant_Trading-20260204/02_macd_strategy_2025.cpp` | MACD 金叉死叉策略回测 |
| `1-AI 量化交易/3-网格策略.py` | `1-AI_Quant_Trading-20260204/03_grid_strategy_2025.cpp` | 网格交易策略回测 |
| `2-金融基础速通/1-K线与成交量.py` | `2-Finance_Basics-20260207/01_kline_volume.cpp` | K 线、成交量与均线可视化 |
| `2-金融基础速通/2-茅台财务指标.py` | `2-Finance_Basics-20260207/02_moutai_financial_indicators.cpp` | 拉取并展示茅台基本面指标 |
| `2-金融基础速通/3-格雷厄姆PB选股.py` | `2-Finance_Basics-20260207/03_graham_pb_stock_picker.cpp` | 基于 PB、PE、ROE 的格雷厄姆风格选股 |
| `2-金融基础速通/4-基本面选股.py` | `2-Finance_Basics-20260207/04_fundamental_stock_picker.cpp` | 综合财务指标多条件选股 |
| `2-金融基础速通/5-茅台MA信号.py` | `2-Finance_Basics-20260207/05_moutai_ma_signals.cpp` | 移动平均线买卖信号 |
| `2-金融基础速通/6-茅台MACD信号.py` | `2-Finance_Basics-20260207/06_moutai_macd_signals.cpp` | MACD 背离与信号可视化 |
| `2-金融基础速通/7-茅台RSI.py` | `2-Finance_Basics-20260207/07_moutai_rsi.cpp` | RSI 超买超卖信号 |
| `2-金融基础速通/8-茅台ATR.py` | `2-Finance_Basics-20260207/08_moutai_atr.cpp` | 真实波幅 ATR 计算与可视化 |
| `2-金融基础速通/9-技术指标仪表盘.py` | `2-Finance_Basics-20260207/09_moutai_indicator_dashboard.cpp` | 多指标综合仪表盘 |
| `2-金融基础速通/10-财务数据下载.py` | `2-Finance_Basics-20260207/10_tushare_financial_data_download.cpp` | 批量下载财务指标数据 |

## 构建

在项目根目录执行：

```powershell
cd C:\Fan\ai-quant-cpp
cmake -B build -S . -G "Ninja"
```

单独编译某个目标：

```powershell
ninja -C build fb_08_moutai_atr
```

目标名前缀规则：

- `ai_xxx`：第一节 `1-AI_Quant_Trading-20260204` 下的程序
- `fb_xxx`：第二节 `2-Finance_Basics-20260207` 下的程序

查看所有目标：

```powershell
ninja -C build -t targets | Select-String -Pattern "^(ai_|fb_)"
```

## 运行

**必须在项目根目录运行**，因为程序使用相对路径 `data/` 和 `outputs/`。

```powershell
cd C:\Fan\ai-quant-cpp
```

### 数据下载类

运行前需要设置 `TUSHARE_TOKEN` 环境变量：

```powershell
$env:TUSHARE_TOKEN = "你的token"
```

```powershell
# 下载寒武纪日线数据
./build/bin/Debug/01_tushare_download_data.exe

# 下载财务数据
./build/bin/Debug/10_tushare_financial_data_download.exe
```

### 策略回测 / 指标计算类

这些程序依赖 `data/600519_SH_daily.csv`：

```powershell
./build/bin/Debug/02_macd_strategy_2025.exe
./build/bin/Debug/03_grid_strategy_2025.exe
./build/bin/Debug/05_moutai_ma_signals.exe
./build/bin/Debug/06_moutai_macd_signals.exe
./build/bin/Debug/07_moutai_rsi.exe
./build/bin/Debug/08_moutai_atr.exe
./build/bin/Debug/09_moutai_indicator_dashboard.exe
```

### 选股类

这些程序依赖 `data/stock_basic.csv`、`data/daily_basic_latest.csv`、`data/fina_indicator_pool.csv`：

```powershell
./build/bin/Debug/03_graham_pb_stock_picker.exe
./build/bin/Debug/04_fundamental_stock_picker.exe
```

## 依赖数据文件

| 程序类别 | 依赖数据 |
|---|---|
| 数据下载类 | 输出到 `data/` 和 `outputs/` |
| 策略/指标类 | `data/600519_SH_daily.csv` |
| 选股类 | `data/stock_basic.csv`、`data/daily_basic_latest.csv`、`data/fina_indicator_pool.csv` |

如果提示找不到 `data/xxx.csv`，请确认在项目根目录运行，或把数据文件放到对应位置。
