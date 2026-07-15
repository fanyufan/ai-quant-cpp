# week6(cpp) — Python 到 C++ 的转换

本目录对应 `week6/` 下的 Python 案例，分为两节课：

- **第 11 课：机器学习因子挖掘**（`11-ML_Factor_Mining-20260321/`）
- **第 12 课：论文复现与策略进化**（`12-Paper_Replication-20260325/`）

Week6 的核心是**机器学习特征工程**、**华泰标准预处理**、**自研 CART/RandomForest 因子挖掘与预测**，以及**复现 MASTER 论文的截面预测方法论**。为避免在 MinGW 上引入 PyTorch、Qlib、XGBoost/LightGBM、Optuna 等构建风险较大的 Python/C++ 库，C++ 版本做如下等价替换：

- 用自研 `RandomForestClassifier` 替代 XGBoost/LightGBM；
- 用穷举/随机网格搜索替代 Optuna 超参优化；
- 跳过依赖 PyTorch/Qlib 的 `2-MASTER截面预测.py`；
- 跳过依赖 `chan.py` 的脚本（week6 中不存在）。

> 说明：以下 Python 脚本保持原样，不转换：
> - `11-机器学习因子挖掘-20260321/CASE-机器学习因子挖掘/2-MASTER截面预测.py`

## 已转换文件

### 11-机器学习因子挖掘（`11-ML_Factor_Mining-20260321/`）

| 原 Python | C++ 文件 | CMake target | 说明 |
|---|---|---|---|
| `1-贵州茅台因子分析.py` | `01_moutai_factor_analysis.cpp` | `w6_ml_01_moutai_factor_analysis` | 52 维技术因子 + 基本面因子、行业哑变量、单因子 RankIC |
| `2-特征工程.py` | `02_feature_engineering.cpp` | `w6_ml_02_feature_engineering` | 多股票特征计算、MAD 去极值、中性化、Z-score、相关性筛选 |
| `3-XGBoost涨跌预测.py` | `03_xgboost_like_prediction.cpp` | `w6_ml_03_xgboost_like_prediction` | 滚动 RandomForest 二分类预测次日涨跌 |
| `4-LightGBM对比与调参.py` | `04_model_comparison.cpp` | `w6_ml_04_model_comparison` | DecisionTree vs RandomForest、网格搜索调参、Purged K-Fold CV |

### 12-论文复现与策略进化（`12-Paper_Replication-20260325/`）

| 原 Python | C++ 文件 | CMake target | 说明 |
|---|---|---|---|
| `1-MASTER数据探索.py` | `01_master_eda.cpp` | `w6_paper_01_master_eda` | 股票池 EDA、52 因子描述统计、高相关性分析、与 MASTER 对比 |
| `3-XGBoost截面预测.py` | `03_cross_sectional_prediction.cpp` | `w6_paper_03_cross_sectional_prediction` | 截面 IC 分析、滚动 RandomForest 截面预测、与 MASTER 指标对比 |

### 公共库扩展

- `common/ml_features.hpp/cpp`：52 维技术因子。
  - 价量：ret_1d/3d/5d/10d、振幅、量比、价量相关性、换手率变化
  - 动量：roc、动量斜率、动量加速度
  - 波动率：ATR 归一化、历史波动率、波动率变化
  - 技术：RSI、ADX、MACD、布林带位置、KDJ、CCI、WILLR、OBV 斜率
  - 均线形态：均线乖离、多头排列得分、上下影线、实体比、新高/新低
  - 交互：动量×波动率、ADX×RSI 等 6 类交叉因子
  - 基本面：PE、ROE、毛利率、负债率
- `common/ml_preprocessing.hpp/cpp`：NaN-aware 统计、MAD 去极值、Z-score、截面预处理、行业市值中性化。
- `common/ml_tree.hpp/cpp`：自研 CART `DecisionTree` 与 `RandomForestClassifier`。
  - 支持 MSE/Gini 分裂、特征子集、特征重要性
  - 二分类/三分类标签构造、滚动训练预测
  - 分类评估（AUC/Accuracy/Precision/Recall/F1）
  - Purged K-Fold CV
  - 因子 IC/RankIC/ICIR/分位数收益评估
- `common/indicators.hpp/cpp`：新增 `stoch`（KDJ）、`cci`、`willr`、`obv`。
- `common/backtest_data_mysql.hpp/cpp`：新增 `batch_load_daily`、`load_financial_data`。

> 注意：模型引擎为教学级自研实现，与 Python 的 XGBoost/LightGBM 在特征重要性、预测概率分布上会有差异，但接口逻辑与实验流程保持一致。

## 构建

在项目根目录执行：

```powershell
cd C:\Fan\ai-quant-cpp
cmake -B build -S . -G Ninja
cmake --build build --config Release
```

week6 目标名：

```text
w6_ml_01_moutai_factor_analysis
w6_ml_02_feature_engineering
w6_ml_03_xgboost_like_prediction
w6_ml_04_model_comparison
w6_paper_01_master_eda
w6_paper_03_cross_sectional_prediction
```

## 运行

**必须在项目根目录运行**，因为程序使用相对路径 `outputs/` 并读取根目录 `.env` 文件。

```powershell
cd C:\Fan\ai-quant-cpp

# 11-机器学习因子挖掘
./build/bin/Release/01_moutai_factor_analysis.exe
./build/bin/Release/02_feature_engineering.exe
./build/bin/Release/03_xgboost_like_prediction.exe
./build/bin/Release/04_model_comparison.exe

# 12-论文复现与策略进化
./build/bin/Release/01_master_eda.exe
./build/bin/Release/03_cross_sectional_prediction.exe
```

## 依赖数据

- 程序默认使用 MySQL `wucai_trade.trade_stock_daily` 表。
- 基本面因子程序会读取 `wucai_trade.trade_stock_financial` 表中的 `eps`、`roe`、`gross_margin`、`debt_ratio` 字段。
- 可用 `week2(cpp)/08_market_data_collection.exe --write-mysql` 灌入数据。
- 数据库连接信息通过根目录 `.env` 文件配置，例如：

```env
DB_HOST=localhost
DB_PORT=3306
DB_USER=quant
DB_PASSWORD=your_password
DB_NAME=wucai_trade
```

## 已知限制

1. **ML 引擎为自研实现**：未绑定 XGBoost/LightGBM，性能与精度与 Python 原版有差异。
2. **超参搜索**：使用穷举/随机网格搜索替代 Optuna，搜索空间较小以保证运行时间可控。
3. **MASTER 截面预测**：使用 50 只代表性大盘股替代 CSI300/CSI800 全市场，股票数量差异会导致 IC/IR 统计噪声更大。
4. **运行时间**：`04_model_comparison.exe` 与 `03_cross_sectional_prediction.exe` 涉及多轮 RandomForest 训练与滚动预测，根据硬件可能需要数分钟到数十分钟。
