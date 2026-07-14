# week2(cpp) — Python 到 C++ 的转换

本目录对应 `week2/` 下的 Python 案例，使用现有 C++ 工具链（`week1(cpp)/common/`）进行了重写。

## 已转换文件

| 原 Python 文件 | C++ 文件 | 说明 |
|---|---|---|
| `3-.../CASE-数据采集/日线数据-tushare.py` | `3-Infrastructure_Building-20260211/CASE-DataCollection/01_daily_data_tushare.cpp` | 调用 `daily` + `adj_factor` 本地计算前复权 |
| `3-.../CASE-数据采集/分钟数据-tushare.py` | `3-Infrastructure_Building-20260211/CASE-DataCollection/02_minute_data_tushare.cpp` | 调用 `stk_mins` 获取 1 分钟 K 线（需单独开通分钟权限） |
| `3-.../CASE-数据采集/财务数据-tushare.py` | `3-Infrastructure_Building-20260211/CASE-DataCollection/03_financial_data_tushare.cpp` | 调用 `fina_indicator` 获取综合财务指标 |
| `3-.../CASE-多因子选股/多因子选股-筛选1.py` | `3-Infrastructure_Building-20260211/CASE-MultiFactorStockPicking/04_multi_factor_filter1.cpp` | 读取 CSV 进行 5 层阈值筛选 |
| `3-.../CASE-多因子选股/多因子选股-筛选2.py` | `3-Infrastructure_Building-20260211/CASE-MultiFactorStockPicking/05_multi_factor_filter2.cpp` | 按行业百分位排名打分并生成分布图 |
| `4-.../CASE-数据采集/7-关键催化剂采集.py` | `4-Data_Acquisition_Cleaning-20260225/CASE-DataCollection/06_key_catalyst_collection.cpp` | 调用 Qwen Max 联网搜索，输出 CSV/SQL 文件（不写 MySQL） |
| `4-.../CASE-数据采集/1-行情数据采集.py` | `4-Data_Acquisition_Cleaning-20260225/CASE-DataCollection/08_market_data_collection.cpp` | 使用 Tushare `daily` 采集全量日线；默认测试模式只采 600519.SH；输出 CSV/SQL，并支持可选的 MySQL 直接写入 |
| `4-.../CASE-数据采集/2-财务数据采集.py` | `4-Data_Acquisition_Cleaning-20260225/CASE-DataCollection/09_financial_data_collection.cpp` | 使用 Tushare `fina_indicator` 采集全量财务指标；默认测试模式，CSV/SQL 输出。注：revenue / net_profit / total_assets / total_equity 来自资产负债表/利润表，当前版本留空，可后续接入 `income`/`balancesheet` 接口补全 |
| `4-.../CASE-数据采集/3-宏观数据采集.py` | `4-Data_Acquisition_Cleaning-20260225/CASE-DataCollection/10_macro_data_collection.cpp` | 使用 Tushare 宏观接口（cpi/ppi/pmi/m/sf/lpr），CSV/SQL 输出 |

## 未转换文件

以下脚本依赖 Python-only 的 SDK（`xtquant/QMT`、`akshare`）或需要 MySQL 写入，保留在 Python 版本中，未做 C++ 转换：

- `3-.../CASE-数据采集/日线数据-akshare.py`、`分钟数据-akshare.py`、`财务数据-akshare.py`  
  （可用已转换的 Tushare 版本替代）
- `3-.../CASE-数据采集/日线数据-QMT.py`、`分钟数据-QMT.py`、`财务数据-QMT.py`  
  （QMT/xtquant 无 C++ 绑定）
- `3-.../CASE-多因子选股/多因子选股-下载数据.py`  
  （依赖 QMT/xtquant）
- `4-.../CASE-数据采集/4-新闻事件采集.py`、`5-研报数据采集.py`、`6-财经日历采集.py`  
  （依赖 akshare 及 MySQL；核心数据来自东方财富/同花顺/百度，需额外对接对应 HTTP 接口）

## 公共库扩展

- `week1(cpp)/common/tushare_client.hpp/cpp`：新增 `adj_factor`、`stk_mins`；补充宏观接口 `cn_cpi`、`cn_ppi`、`cn_pmi`、`cn_m`、`sf_month`、`lpr_data`。
- `week1(cpp)/common/rank.hpp/cpp`：新增行业内分组百分位排名 helpers。
- `week1(cpp)/common/plotter.hpp/cpp`：新增水平柱状图 helper（当前 gnuplot 后端会回退为垂直柱状图并给出提示）。
- `week1(cpp)/common/mysql_client.hpp/cpp`：基于 MariaDB C Connector 的简单 MySQL C API 封装。
- 项目集成 `third_party/mariadb-connector-c-3.4.9/` 源码，通过 `add_subdirectory` 与 MinGW 一起构建，不依赖 vcpkg。

## 构建

```bash
cmake -B build -S .
cmake --build build
```

## 运行示例

```bash
# 日线/分钟/财务/行情/宏观数据需要 TUSHARE_TOKEN
set TUSHARE_TOKEN=your_token
./build/bin/Debug/01_daily_data_tushare.exe
./build/bin/Debug/03_financial_data_tushare.exe
./build/bin/Debug/08_market_data_collection.exe          # 测试模式：600519.SH
./build/bin/Debug/08_market_data_collection.exe --full   # 全量 A 股
./build/bin/Debug/08_market_data_collection.exe --write-mysql --mysql-user root --mysql-password your_pass --mysql-db quant  # 同时写入 MySQL
./build/bin/Debug/08_market_data_collection.exe --write-mysql                                        # 从 .env 读取数据库配置
./build/bin/Debug/09_financial_data_collection.exe       # 测试模式：600519.SH
./build/bin/Debug/10_macro_data_collection.exe

# 多因子选股（需要输入 CSV）
./build/bin/Debug/04_multi_factor_filter1.exe --input data/stock_fina_pool_QMT.csv --output data/stock_fina_selected_QMT.csv
./build/bin/Debug/05_multi_factor_filter2.exe --input data/stock_fina_pool_QMT.csv --output data/stock_fina_selected_QMT_industry.csv --viz-dir data/industry_viz

# 关键催化剂采集需要 DASHSCOPE_API_KEY
set DASHSCOPE_API_KEY=your_key
./build/bin/Debug/06_key_catalyst_collection.exe --output-dir data
```

### 使用 `.env` 配置 MySQL

`08_market_data_collection.exe` 支持从项目根目录的 `.env` 文件读取数据库配置：

```bash
MYSQL_HOST=localhost
MYSQL_PORT=3306
MYSQL_USER=root
MYSQL_PASSWORD=your_password
MYSQL_DB=quant
```

然后直接运行：

```bash
./build/bin/Debug/08_market_data_collection.exe --write-mysql
```

命令行参数（`--mysql-host`、`--mysql-user` 等）会覆盖 `.env` 中的同名配置。

注意：Windows 下运行请确保工作目录为项目根目录，或显式指定 `--input`/`--output-dir`/`--env-file` 路径。
