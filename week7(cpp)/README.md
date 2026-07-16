# week7(cpp) — QuantStats 绩效分析与报告 / RAG 投研系统

本目录对应原课程：

- `13-QuantStats绩效分析与报告-20260328`
- `14-RAG投研系统搭建-20260401`

## 转换范围

只转换了 2 个脚本，其余脚本因强依赖 Python 生态而保留原 Python 实现：

| 原脚本 | C++ 可执行文件 | 说明 |
|--------|----------------|------|
| `2-SVD因子挖掘与分析.py` | `02_svd_factor_mining.exe` | SVD 因子挖掘、滚动 SVD、因子压缩与溯源、残差分析 |
| `3-实盘交易绩效分析.py` | `03_live_trade_report.exe` | 读取券商成交 CSV，构建净值曲线，计算常用绩效指标，输出图表与文本报告 |
| `1-QuantStats绩效分析.py` | — | **跳过**：Backtrader + QuantStats + LLM + HTML 自研等价代码量过大 |
| `4-实盘交易绩效分析Plus.py` | — | **跳过**：在脚本 3 基础上增加 LLM 结论生成，保留 Python 版本 |
| `14-RAG投研系统搭建-20260401` 全部脚本 | — | **跳过**：依赖 embedding/FAISS/RAG/word2vec，C++ 引入这些库构建风险高 |

## 新增公共组件

- `common/svd.hpp`：轻量 header-only Jacobi SVD 实现，避免新增 vcpkg 依赖。
  - 通过 `A^T A` 或 `A A^T` 的特征分解得到奇异值/左右奇异向量。
  - 适用于收益率矩阵（N × T，N 为股票数）和特征矩阵（样本 × 特征）等中小规模稠密矩阵。

## 依赖

- 已集成的 `quant_utils` / `quant_mysql`：MySQL 日 K 加载、52 维技术因子、RandomForest、CSV/`.env` 工具、Matplot++ 绘图。
- 数据依赖：MySQL `wucai_trade.trade_stock_daily`。

## 构建

```bash
cd build
cmake -DCMAKE_BUILD_TYPE=Release -G Ninja ..
ninja w7_qs_02_svd_factor_mining w7_qs_03_live_trade_report
```

可执行文件位于 `build/bin/Release/`。

## 运行

### 02 — SVD 因子挖掘与分析

```bash
./build/bin/Release/02_svd_factor_mining.exe
```

输出：

- 控制台：SVD 分解结果、因子解释、行业结构、滚动 SVD 统计、因子压缩对比、残差分析。
- `outputs/week7/svd_scree_plot.png`
- `outputs/week7/svd_factor_timeseries.png`
- `outputs/week7/svd_rolling_concentration.png`

### 03 — 实盘交易绩效分析

```bash
# 指定一个或多个券商导出的成交 CSV
./build/bin/Release/03_live_trade_report.exe data/sample_trades.csv
```

CSV 列名支持中文常见券商格式（如 `成交日期`、`证券代码`、`操作`、`成交数量`、`成交均价`、`成交金额`、`手续费` 等），并会自动过滤 A 股（代码以 0/3/6 开头）。

输出：

- 控制台：账户概要、绩效指标。
- `outputs/week7/live_trade_report.txt`
- `outputs/week7/live_trade_report_nav.csv`
- `outputs/week7/live_trade_report_chart.png`

## 与 Python 原脚本的差异

- SVD 使用自研 Jacobi 方法，而不是 `numpy.linalg.svd`。
- 因子压缩对比使用自研 `RandomForestClassifier`（`common/ml_tree`），而不是 XGBoost。
- 实盘绩效分析不生成 QuantStats HTML 报告，改为控制台 + 文本报告 + Matplot++ 图表。
- 未实现 LLM 结论生成、HTML 渲染、embedding/FAISS/RAG/word2vec 等功能。
