# week8(cpp) | 智能研报生成（财务分析工具）

本目录包含 `week8/15-智能研报生成-20260404/skills/financial-analysis/scripts/` 下 2 个纯财务分析工具的 C++17 转换实现。

> **范围说明**：根据课程安排，Week8 仅转换财务分析脚本。LLM Agent、PDF 解析、FAISS/RAG、舆情情感、nanobot 等模块本次**全部跳过**。

---

## 目录结构

```text
week8(cpp)/
└── 15-Smart_Report_Generation-20260404/
    ├── 01_financial_ratio_analysis.cpp   # 单股票核心财务指标分析
    └── 02_peer_comparison.cpp            # 多股票横向财务对比
```

---

## 新增公共模块

- `common/financial_abstract.hpp` / `.cpp`
  - 统一财务摘要加载接口，同时支持：
    - **CSV**：`data/financial_data/{code}_financial_abstract.csv`（akshare 风格）
    - **MySQL**：`wucai_trade.trade_stock_financial`
  - 提供 13 个核心财务指标配置与名称映射。

---

## 构建目标

| CMake target | 输出可执行文件 | 说明 |
|--------------|----------------|------|
| `w8_sr_01_financial_ratio_analysis` | `01_financial_ratio_analysis.exe` | 单股票财务比率分析 |
| `w8_sr_02_peer_comparison` | `02_peer_comparison.exe` | 同行财务对比 |

构建命令：

```powershell
cd C:\Fan\ai-quant-cpp
ninja -C build w8_sr_01_financial_ratio_analysis w8_sr_02_peer_comparison
```

---

## 运行示例

**必须在项目根目录运行**，程序使用相对路径 `data/` 和 `outputs/`。

### 1. 单股票财务比率分析

```powershell
.\build\bin\Release\01_financial_ratio_analysis.exe `
  --stock 600519 `
  --data_dir data\financial_data `
  --years 5
```

参数：

- `--stock` / `-s`：股票代码（6 位数字或带后缀，如 `600519` 或 `600519.SH`）
- `--years`：分析最近 N 年的年报数据（默认 5）
- `--data_dir`：CSV 数据目录（默认 `week8/15-智能研报生成-20260404/data/financial_data`）
- `--output`：JSON 输出路径（默认 `outputs/week8/{code}_financial_ratio_analysis.json`）
- `--mysql`：强制从 MySQL 加载（需配置 `.env`）

输出：

- 控制台报告（指标历史、同比、趋势、综合评价）
- JSON：`outputs/week8/{code}_financial_ratio_analysis.json`
- 图表：`outputs/week8/{code}_financial_ratio_analysis.png`

### 2. 同行财务对比

```powershell
.\build\bin\Release\02_peer_comparison.exe `
  --stocks 600519,000001 `
  --data_dir data\financial_data
```

参数：

- `--stocks`：逗号分隔的股票代码列表（至少 2 只）
- `--data_dir`：CSV 数据目录
- `--output`：JSON 输出路径（默认 `outputs/week8/peer_comparison.json`）
- `--mysql`：强制从 MySQL 加载

输出：

- 控制台对比表（标记各指标最优公司）
- JSON：`outputs/week8/peer_comparison.json`
- 图表：`outputs/week8/peer_comparison.png`

---

## 数据来源

### CSV 数据格式

文件命名：`{6位股票代码}_financial_abstract.csv`

示例 `data/financial_data/600519_financial_abstract.csv`：

```csv
指标,20241231,20231231,20221231,20211231,20201231
营业总收入,15000000000,14000000000,12000000000,10000000000,9000000000
净利润,7000000000,6500000000,5500000000,4500000000,4000000000
毛利率,50,49,48,47,46
...
```

首列为指标中文名，后续列为报告期（YYYYMMDD），数值使用原始单位（元、百分比等）。

### MySQL 数据

需要项目根目录 `.env` 文件配置 MySQL 连接：

```ini
MYSQL_HOST=localhost
MYSQL_PORT=3306
MYSQL_USER=root
MYSQL_PASSWORD=your_password
MYSQL_DATABASE=wucai_trade
```

程序会自动读取 `.env` 并连接 `wucai_trade.trade_stock_financial` 表。

---

## 与 Python 原版的差异

- 移除 LLM 研报生成、PDF 解析、FAISS 向量检索等非财务模块。
- 使用 C++17 自研分析逻辑替代 Python 中的 pandas/openpyxl。
- 图表使用 Matplot++ 替代 matplotlib/seaborn。
- JSON 输出保持与 Python 版本一致的核心字段。

---

## 常见问题

### 找不到 CSV 文件

确保 `data/financial_data/{code}_financial_abstract.csv` 存在，或通过 `--data_dir` 指定正确目录。

### MySQL 连接失败

检查 `.env` 文件是否存在且配置正确，并确认 MySQL 服务已启动。

### 图表无法保存

Matplot++ 依赖 gnuplot，请确保 gnuplot 已安装并加入系统 PATH。
