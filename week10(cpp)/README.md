# week10(cpp) | Kris 风控体系与 ATR 风控实战

本目录包含 `week10/19-强化学习与风控体系-20260418/CASE-Kris的风控体系/` 中可独立转换为 C++17 的最小集脚本。根据项目约定，**依赖 akshare、LLM、PyTorch、sklearn、LangGraph、MiniQMT 的脚本全部跳过**。

---

## 目录结构

```text
week10(cpp)/
└── 19-Risk_System-20260418/
    ├── 01_kris_risk_engine.cpp   # Kris 风控引擎 8 条规则演示
    └── 02_atr_risk_demo.cpp      # ATR 仓位、ATR 止损 vs 固定止损
```

---

## 新增公共模块

- `common/risk_engine.hpp`：Kris 风控引擎统一头文件，包含：
  - 事前规则：单笔金额上限、价格偏离、ST/黑名单、ATR 仓位
  - 事中规则：单日亏损熔断、ATR 止损
  - 外部信号：事件关键词、宏观 VIX 门控
  - `EventLLMChecker`（大模型审批）明确跳过

---

## 构建目标

| CMake target | 输出可执行文件 | 说明 |
|--------------|----------------|------|
| `w10_rs_01_kris_risk_engine` | `01_kris_risk_engine.exe` | 风控引擎单笔审批 + 内置演示 |
| `w10_rs_02_atr_risk_demo` | `02_atr_risk_demo.exe` | ATR 风控实战演示 |

构建命令：

```powershell
cd C:\Fan\ai-quant-cpp
ninja -C build w10_rs_01_kris_risk_engine w10_rs_02_atr_risk_demo
```

---

## 运行示例

**必须在项目根目录运行**。

### 1. Kris 风控引擎

内置演示（对应 Python `1-风控引擎.py` 主程序）：

```powershell
.\build\bin\Release\01_kris_risk_engine.exe --demo
```

单笔订单审批：

```powershell
.\build\bin\Release\01_kris_risk_engine.exe `
  --code 600519.SH --direction buy --amount 800000 --price 1700 `
  --current_price 1700 --atr 30 --total_asset 1000000 --vix 18.5 `
  --news "茅台业绩稳定增长"
```

参数：

- `--code`：股票代码
- `--direction`：`buy` / `sell`
- `--amount` / `--price` / `--quantity`：订单金额、价格、数量
- `--total_asset`：总资产
- `--current_price`：现价（用于价格偏离检查）
- `--atr`：ATR 值（用于 ATR 仓位检查）
- `--vix`：VIX 值（用于宏观门控）
- `--news`：新闻文本（用于事件关键词检查）
- `--max_order_amount` / `--price_collar_pct` / `--max_daily_loss_pct`：风控阈值
- `--output`：JSON 输出路径

输出：

- 控制台逐条规则审批结果
- `outputs/week10/kris_risk_engine.json`

### 2. ATR 风控实战

```powershell
.\build\bin\Release\02_atr_risk_demo.exe --code 600519
```

参数：

- `--code`：股票代码（默认 `510050`）
- `--start` / `--end`：日期过滤 `YYYYMMDD`
- `--data_dir`：CSV 目录（默认 `data`）
- `--total_asset`：总资产（默认 1,000,000）
- `--initial_cash`：Demo2 入场资金（默认 1,000,000）
- `--mysql`：CSV 缺失时从 MySQL 加载
- `--output`：JSON 输出路径

输出：

- Demo1：最近 120 日低/高波动期 ATR 仓位对比
- Demo2：ATR 止损 vs 固定 5% 止损对比
- Demo3：Kris ATR 止损循环调用演示
- `outputs/week10/{code}_atr_risk_demo.json`
- `outputs/week10/{code}_atr_risk_demo.png`

---

## 数据源

CSV 文件命名规则：`data/{code}_{SH|SZ}_daily.csv`。项目已提供：

- `data/600519_SH_daily.csv`
- `data/688256_SH_daily.csv`

MySQL 需要 `.env` 配置，表为 `wucai_trade.trade_stock_daily`。

---

## 与 Python 原版的差异

| Python 脚本 | C++ 处理 |
|-------------|----------|
| `1-风控引擎.py` | 完整转换 8 条规则；删除 `EventLLMChecker`；命令行支持单笔审批 |
| `2-ATR风控实战.py` | 用 CSV/MySQL 替换 `data_loader`；自研 Wilder ATR；保留三个 Demo；输出 JSON+图表 |
| `3-事件风控实战.py` | 跳过（akshare + LLM） |
| `4-宏观门控实战.py` | 作为规则集成进 `common/risk_engine.hpp`，不在本最小集单独演示 |
| `6-高频做市模拟.py` / `7-主力行为识别.py` | 跳过（PyTorch / sklearn） |
| `20/.../LangGraph 工作流` | 全部跳过 |

---

## 常见问题

### 程序启动时报 illegal instruction

本项目使用 fmt 进行格式化，请勿在 `fmt::print` 格式串中使用 `{:,.0f}` 等带逗号的浮点说明符。金额已改用 `{:.0f}` 输出。

### 找不到 CSV 文件

确认 `data/{code}_SH_daily.csv` 或 `{code}_SZ_daily.csv` 存在，或用 `--data_dir` 指定目录。

### 图表无法保存

Matplot++ 依赖 gnuplot，请确保 gnuplot 在 PATH 中。
