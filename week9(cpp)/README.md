# week9(cpp) | XtQuant 信号/回测与强化学习入门

本目录包含 `week9/` 中可独立转换为 C++17 的最小集脚本。根据项目约定，**依赖 xtquant/MiniQMT、LLM、FAISS、PDF 解析、PyTorch/SB3 的脚本全部跳过**。

---

## 目录结构

```text
week9(cpp)/
├── 17-Xtquant_Live_Trading-20260411/
│   ├── 01_macd_double_ma_backtest.cpp   # MACD / 双均线策略回测
│   └── 02_macd_signal_engine.cpp        # MACD 金叉/死叉信号检查
└── 18-Reinforcement_Learning-20260415/
    ├── 01_cartpole_qlearning.cpp        # 手写 CartPole 环境 + Q-learning
    └── 02_maze_qlearning.cpp            # 6 节点迷宫 Q-learning
```

---

## 新增公共模块

- `common/rl/discretizer.hpp`：连续状态分箱编码，供 CartPole 等环境使用。
- `common/rl/qlearning.hpp`：通用 Q-table 与 ε-greedy Agent。

---

## 构建目标

| CMake target | 输出可执行文件 | 说明 |
|--------------|----------------|------|
| `w9_xt_01_macd_double_ma_backtest` | `01_macd_double_ma_backtest.exe` | 策略回测 |
| `w9_xt_02_macd_signal_engine` | `02_macd_signal_engine.exe` | MACD 信号 |
| `w9_rl_01_cartpole_qlearning` | `01_cartpole_qlearning.exe` | CartPole RL |
| `w9_rl_02_maze_qlearning` | `02_maze_qlearning.exe` | 迷宫 RL |

构建命令：

```powershell
cd C:\Fan\ai-quant-cpp
ninja -C build w9_xt_01_macd_double_ma_backtest w9_xt_02_macd_signal_engine w9_rl_01_cartpole_qlearning w9_rl_02_maze_qlearning
```

---

## 运行示例

**必须在项目根目录运行**。

### 1. MACD / 双均线回测

```powershell
.\build\bin\Release\01_macd_double_ma_backtest.exe --code 600519 --strategy macd --count 250
.\build\bin\Release\01_macd_double_ma_backtest.exe --code 600519 --strategy double_ma --count 250
```

参数：

- `--code`：股票代码（6 位或带后缀，如 `600519` / `600519.SH`）
- `--strategy`：`macd` 或 `double_ma`
- `--start` / `--end`：日期过滤 `YYYYMMDD`
- `--count`：使用最近 N 根 K 线（默认 250）
- `--data_dir`：CSV 目录（默认 `data`）
- `--mysql`：CSV 缺失时从 MySQL 加载
- `--output`：JSON 输出路径

输出：

- 控制台绩效报告
- `outputs/week9/{code}_{strategy}_backtest.json`
- `outputs/week9/{code}_{strategy}_backtest.png`

### 2. MACD 信号引擎

```powershell
.\build\bin\Release\02_macd_signal_engine.exe --code 600519 --count 250
```

输出最近 5 个交易日 MACD 指标与最新信号，JSON/PNG 保存到 `outputs/week9/`。

### 3. CartPole Q-learning

```powershell
.\build\bin\Release\01_cartpole_qlearning.exe --episodes 100
```

手写 CartPole-v1 物理环境，使用离散化状态 Q-learning 训练。输出训练曲线到 `outputs/week9/cartpole_qlearning.json/png`。

### 4. 迷宫 Q-learning

```powershell
.\build\bin\Release\02_maze_qlearning.exe
```

复现 Python `maze.py` 的固定奖励矩阵迭代，输出归一化 Q 表与最优路径。

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
| `run_backtest.py` | 去掉 `xtdata`，改为 CSV/MySQL；保留 MACD + 双均线回测逻辑 |
| `5-signal_to_order.py` | 只保留信号层，删除 MiniQMT 下单循环 |
| `cartpole.py` / `agent.py` | 手写环境物理，替换 `gymnasium` 与 `numpy` |
| `maze.py` | 用 `std::vector` 替代 `numpy`，输出 JSON/图表 |

---

## 常见问题

### 找不到 CSV 文件

确认 `data/{code}_SH_daily.csv` 或 `{code}_SZ_daily.csv` 存在，或用 `--data_dir` 指定目录。

### 图表无法保存

Matplot++ 依赖 gnuplot，请确保 gnuplot 在 PATH 中。

### CartPole 训练不稳定

Q-learning 对离散化边界敏感，可调整 `--episodes` 或修改源码中的 `bins` / `lows` / `highs`。
