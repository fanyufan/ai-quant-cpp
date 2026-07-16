# week11(cpp) | 投资晨会（板块轮动 + 多因子选股）与龙头战法

本目录包含 `week11/21-投资晨会-20260425/` 与 `week11/22-实盘作战与CEO控制台-20260429/` 中可独立转换为 C++17 的 4 个核心脚本。根据项目约定，**依赖 xtdata（miniQMT）、LLM、LangGraph、Flask/FastAPI/Gradio、APScheduler、sklearn、webhook 推送的脚本全部跳过**。

---

## 目录结构

```text
week11(cpp)/
├── 21-Morning_Meeting-20260425/
│   ├── 01_sector_rotation.cpp           # 板块轮动综合视图（强度 + 拐点相位）
│   └── 02_factor_layered_backtest.cpp   # 多因子选股分层回测
└── 22-Live_Trading-20260429/
    ├── 03_dragon_picker.cpp             # 龙头战法选股（mock demo + MySQL 实盘扫描）
    └── 04_dragon_backtest.cpp           # 龙头战法全市场 T+1 回测
```

---

## 新增公共模块

- `common/sector_rotation.hpp`：板块轮动算法库（header-only，命名空间 `quant::sector`）
  - 一阶导：ROC_20、MA20_SLOPE（10 日窗口 OLS 年化斜率）
  - 二阶导：MACD_HIST（EMA12/26/9）、MA20_ACCEL（斜率 5 日差分）
  - 强度三指标：MOM_21、RS_60（相对等权基准）、VOL_RATIO → 横截面 Z-score 等权
  - 四象限拐点：速度投票 + 加速度投票 → accel_up / decel_up / accel_down / decel_down / neutral
  - 综合分：composite = score + PHASE_BONUS（+3 / +2 / +0.5 / 0 / -2）
- `common/factor_lib.hpp`：多因子库（header-only，命名空间 `quant::factor`）
  - 10 因子：MOM_1M/3M/6M、REV_5D、VOL_20/60、LIQ_20、TURN_20、RSI_14、BIAS_20
  - 预处理：MAD 去极值（3×1.4826×MAD）、Z-score、行业中性化
  - 合成：等权 / IC 加权（walk-forward）；IC 支持 spearman / pearson
- `common/dragon_picker.hpp`：龙头战法核心（header-only，命名空间 `quant::dragon`）
  - v1 五法则 + v2 三补丁硬过滤（涨幅 5%~9.5%、涨幅榜前 50、市值 30-500 亿、量比 ≥2、价格 <30、ST/次新排除、板块共振）
  - 六维 dragon_score 打分；入场参数（止损/目标/股数，风险 1%、盈亏比 2:1）

---

## 构建目标

| CMake target | 输出可执行文件 | 说明 |
|--------------|----------------|------|
| `w11_mm_01_sector_rotation` | `01_sector_rotation.exe` | 板块轮动综合视图 |
| `w11_mm_02_factor_backtest` | `02_factor_layered_backtest.exe` | 多因子分层回测 |
| `w11_lt_03_dragon_picker` | `03_dragon_picker.exe` | 龙头战法选股 |
| `w11_lt_04_dragon_backtest` | `04_dragon_backtest.exe` | 龙头战法全市场回测 |

构建命令：

```powershell
cd C:\Fan\ai-quant-cpp
ninja -C build w11_mm_01_sector_rotation w11_mm_02_factor_backtest w11_lt_03_dragon_picker w11_lt_04_dragon_backtest
```

---

## 运行示例

**必须在项目根目录运行**（输出写入 `outputs/week11/`，`.env` 从项目根读取）。

### 1. 板块轮动综合视图（对应 CASE-B derivatives + industry_strength + inflection_detector + run_today）

```powershell
.\build\bin\Release\01_sector_rotation.exe --level 2 --top 10 --lookback 90
```

参数：

- `--level`：申万板块级别 1 / 2（默认 2）
- `--end`：截止日期 `YYYY-MM-DD`（默认最新交易日）
- `--top`：综合推荐 Top N（默认 10）
- `--lookback`：图表回看天数（默认 90）

输出：

- 控制台：Top N 综合表、相位分组统计、Bottom 5
- `outputs/week11/sector_combined.csv`、`sector_strength.csv`、`sector_phase.csv`
- `outputs/week11/sector_today_top{N}.txt`、`sector_top{N}.png`

### 2. 多因子选股分层回测（对应 CASE-C factor_lib + preprocessor + synthesizer + layered_backtest）

```powershell
.\build\bin\Release\02_factor_layered_backtest.exe --max-stocks 80 --lookback 400 --rebal 21 --weight equal
```

参数：

- `--pool`：股票池文件（默认 `data/csi300_codes.txt`，一行一个 `XXXXXX.SH/SZ`）
- `--max-stocks`：参与股票上限（默认 80，可调大至 299）
- `--lookback`：每股回看 K 线根数（默认 400）
- `--rebal`：调仓周期交易日数（默认 21）
- `--ic-lookback`：IC 加权回看期数（默认 6）
- `--weight`：`equal`（等权）/ `ic`（IC 加权 walk-forward）
- `--start` / `--end`：回测区间（默认全部可用日期）

输出：

- 控制台：5 层收益表（累计/年化/MDD/Sharpe）、合成 IC/IR、单因子 IC 排名、Top-N 集中度
- `outputs/week11/factor_layer_nav.csv`、`factor_ic.csv`、`factor_backtest_summary.json`
- `outputs/week11/factor_layer_nav.png`（5 层 + 基准 + 多空净值曲线）

### 3. 龙头战法选股（对应 22 章 dragon_picker.py）

无参内置 mock demo（12 只代表性股票，v1/v2 法则逐条对照）：

```powershell
.\build\bin\Release\03_dragon_picker.exe
```

MySQL 实盘扫描（最近交易日全市场）：

```powershell
.\build\bin\Release\03_dragon_picker.exe --mysql --top 5
```

参数：`--top`、`--min-change 0.05`、`--max-change 0.095`、`--max-price 30`、`--min-vol-ratio 2.0`、`--mcap-low 30e8`、`--mcap-high 500e8`、`--min-listed-days 60`、`--no-sector-resonance`（v1 对照）、`--sector-level 2`

输出：控制台明细 + `outputs/week11/dragon_picker.json`

### 4. 龙头战法全市场 T+1 回测（对应 22 章 dragon_backtest.py）

```powershell
.\build\bin\Release\04_dragon_backtest.exe --start 20250101 --end 20260401 --top 5 --hold 1,3,5
```

参数：选股阈值同 03，另有 `--start` / `--end`（默认 2025-01-01 ~ 2026-04-01）、`--hold 1,3,5`

口径：信号日 T 收盘选股 → T+1 开盘价买 → T+H 收盘价卖；**无手续费/滑点/印花税/仓位管理，无涨跌停成交模拟**（与 Python 原版毛收益口径一致）

输出：

- 控制台：各持有期胜率/均收/中位/最优/最差 + 净值年化/Sharpe/MDD + sector_2 汇总
- `outputs/week11/dragon_trades_H{h}.csv`、`dragon_curve_H{h}.csv`
- `outputs/week11/dragon_nav.png`（多持有期净值对比）

---

## 数据源

MySQL（必需，`.env` 配置 `MYSQL_*`，库 `wucai_trade`）：

- `trade_stock_daily`：全市场日 K（7000+ 股，2021-01 起）
- `trade_stock_status`：股票元信息（名称/申万行业/流通股本/上市日期）
- `trade_sector_daily`：板块指数与涨跌统计（131 个板块）

股票池快照（gitignore 不进版本库，需手动拷贝一次）：

```powershell
copy "week11\21-投资晨会-20260425\CASE-C-多因子选股\data\csi300_codes.txt" data\csi300_codes.txt
```

---

## 与 Python 原版的差异

| Python 脚本 | C++ 处理 |
|-------------|----------|
| 21 CASE-B 四脚本 | 合并为 `01_sector_rotation`，算法 1:1 移植 |
| 21 CASE-C layered_backtest 等 | `02_factor_layered_backtest`；基准用**池内等权**替代沪深300指数（无指数数据）；Lasso 合成跳过（sklearn） |
| 21 CASE-A 数据准备 | 跳过（xtdata 采集管道，数据已在库） |
| 21 CASE-D 晨会工作流 | 跳过（LangGraph / 钉钉 webhook / APScheduler） |
| 22 dragon_picker.py | 完整移植 + 新增 `--mysql` 实盘扫描模式 |
| 22 dragon_backtest.py | 完整移植，逐日全市场内存扫描（300 交易日 ≈ 26 秒） |
| 22 CASE-AI量化系统 Web 层 | 跳过（FastAPI / Gradio / routes / templates） |
| 22 live_trading / live_loop / miniqmt | 跳过（xtquant 实时行情与券商网关） |
| 22 morning_brief / alerting / scheduler | 跳过（LLM / webhook / SMTP / APScheduler） |
| third_party/nanobot | 跳过（外部 LLM 项目） |

已知口径说明：

- `trade_sector_daily.change_pct` 为百分数，使用前 `/100`
- `listed_days` 为自然日（非交易日）
- 多因子基准为池内等权（Python 原版为沪深300指数）

---

## 常见问题

### 程序启动时报 illegal instruction

本项目使用 fmt 进行格式化，请勿在 `fmt::print` 格式串中使用 `{:,.0f}` 等带逗号的浮点说明符。金额已改用 `{:.0f}` 输出。

### MySQL 连接失败

确认项目根 `.env` 的 `MYSQL_HOST/PORT/USER/PASSWORD/DB` 正确，且 `wucai_trade` 库含上述三张表。

### 找不到股票池文件

执行上文「数据源」中的拷贝命令，或用 `--pool` 指定路径。

### 图表无法保存

Matplot++ 依赖 gnuplot，请确保 gnuplot 在 PATH 中。
