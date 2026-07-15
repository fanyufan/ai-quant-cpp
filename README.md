# ai-quant-cpp

高性能 AI 量化交易系统实战 | C++ 工程化 × 策略回测 × 低延迟执行 🚀

本项目将 `week1/` ~ `week4/` 目录下的 Python 量化脚本转换为 C++17 实现，使用 CMake + vcpkg 管理依赖，Ninja 构建，Matplot++ 绘图。

---

## 环境要求

- Windows 10/11
- [w64devkit](https://github.com/skeeto/w64devkit)（GCC + Ninja）
- [CMake](https://cmake.org/) >= 3.20
- [vcpkg](https://vcpkg.io/)（已集成到 CMake toolchain）
- [gnuplot](http://www.gnuplot.info/)（Matplot++ 后端，需加入 PATH）
- Tushare Pro 账号及 Token（数据下载脚本需要）

---

## 项目结构

```text
ai-quant-cpp/
├── .vscode/
│   └── settings.json       # VS Code CMake 配置
├── CMakeLists.txt          # CMake 主配置
├── vcpkg.json              # vcpkg 依赖清单
├── common/                 # 共用 C++ 库：csv / date / indicators / plotter / tushare_client / mysql_client
├── week1/                  # 原始 Python 脚本（中文目录名）
├── week1(cpp)/             # week1 的 C++ 转换实现
│   ├── 1-AI_Quant_Trading-20260204/
│   └── 2-Finance_Basics-20260207/
├── week2/                  # 原始 Python 脚本（中文目录名）
├── week2(cpp)/             # week2 的 C++ 转换实现
│   ├── 3-Infrastructure_Building-20260211/
│   └── 4-Data_Acquisition_Cleaning-20260225/
├── week3/                  # 原始 Python 脚本（中文目录名）
├── week3(cpp)/             # week3 的 C++ 转换实现
│   ├── 5-Backtrader_Backtesting-20260228/
│   └── 6-Talib_Technical_Indicators-20260304/
├── week4/                  # 原始 Python 脚本（中文目录名）
├── week4(cpp)/             # week4 的 C++ 转换实现
│   └── 8-Turtle_Trading-20260311/
└── build/                  # 构建输出目录（.gitignore 忽略）
    └── bin/Debug/          # 可执行文件
```

---

## 依赖

`vcpkg.json` 中声明：

- `matplotplusplus` — 绘图
- `nlohmann-json` — JSON 解析
- `cpr` — HTTP 请求
- `fmt` — 格式化输出

---

## VS Code 配置（推荐）

项目已提供 `.vscode/settings.json`，打开项目时会自动：

- 使用 Ninja 作为生成器
- 指定 vcpkg toolchain
- 显式设置 `CMAKE_PREFIX_PATH` 指向 manifest 模式安装目录，避免 CMake 找不到 vcpkg 包
- 设置 `x64-mingw-dynamic` triplet
- 配置 IntelliSense 使用 CMake Tools 作为 provider

如果 `CMakeLists.txt` 中 `find_package(...)` 报红，按 `Ctrl+Shift+P` → `CMake: Configure` 重新配置即可。

---

## 完整流程

### 1. 配置（首次或 CMakeLists.txt / vcpkg.json 变更后执行）

```powershell
cd C:\Fan\ai-quant-cpp
cmake -B build -S . -G "Ninja" `
  -DCMAKE_TOOLCHAIN_FILE=C:\Fan\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\Fan\ai-quant-cpp\build\vcpkg_installed\x64-mingw-dynamic `
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic `
  -DVCPKG_HOST_TRIPLET=x64-mingw-dynamic `
  -DVCPKG_MANIFEST_MODE=ON
```

说明：
- `-B build`：构建目录
- `-S .`：源码目录
- `-G "Ninja"`：使用 Ninja 构建
- `CMAKE_TOOLCHAIN_FILE`：接入 vcpkg
- `CMAKE_PREFIX_PATH`：显式指定 vcpkg manifest 安装目录，解决部分环境下 CMake 找不到包的问题
- `VCPKG_TARGET_TRIPLET` / `VCPKG_HOST_TRIPLET`：指定 MinGW 64 位动态链接
- `VCPKG_MANIFEST_MODE=ON`：根据 `vcpkg.json` 自动安装依赖

配置成功后会看到 `-- Running vcpkg install ... done` 和 `-- Generating done`。

---

### 2. 编译

**编译全部：**

```powershell
ninja -C build -j 4
```

**单独编译某个目标：**

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

编译完成后，可执行文件位于：

```text
build/bin/Debug/
```

---

### 3. 运行

**必须在项目根目录运行**，因为程序使用相对路径 `data/` 和 `outputs/`。

```powershell
cd C:\Fan\ai-quant-cpp
```

#### 3.1 week1(cpp) 策略/指标/选股案例

`week1(cpp)/` 下的 C++ 转换脚本（数据下载、MACD/网格策略、茅台指标、选股等）使用方法详见 `week1(cpp)/README.md`，包括目标名前缀、运行示例和依赖数据说明。

#### 3.2 week2(cpp) 数据/因子案例

`week2(cpp)/` 下的 C++ 转换脚本（数据采集、多因子选股、关键催化剂等）使用方法详见 `week2(cpp)/README.md`，包括 Tushare Token、MySQL `.env` 配置、各脚本参数及常见问题。

#### 3.3 week3(cpp) 回测与技术指标

`week3(cpp)/` 下的 C++ 转换脚本（Backtrader 风格回测、TA-Lib 指标/形态、选股雷达等）使用方法详见 `week3(cpp)/README.md`，包括回测引擎用法、指标演示和 MySQL 选股雷达。

#### 3.4 week4(cpp) 海龟交易法则

`week4(cpp)/` 下的 C++ 转换脚本（经典海龟、ADX 过滤海龟、多周期海龟、ML 增强海龟）使用方法详见 `week4(cpp)/README.md`，包括海龟策略参数、CSV/MySQL 数据源和 C++ 轻量决策树说明。

---

## 常见问题

### 1. `vcpkg-running.lock: waiting to take filesystem lock`

vcpkg 进程没正常退出。解决：

```powershell
taskkill /F /IM vcpkg.exe
Remove-Item -Force C:\Fan\ai-quant-cpp\build\vcpkg_installed\vcpkg\vcpkg-running.lock
```

### 2. `Could not find a package configuration file provided by "Matplot++"`

说明 `vcpkg_installed` 里的包损坏或没装好。删掉重建：

```powershell
Remove-Item -Recurse -Force C:\Fan\ai-quant-cpp\build\vcpkg_installed
cmake -B build -S . -G "Ninja" `
  -DCMAKE_TOOLCHAIN_FILE=C:\Fan\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic `
  -DVCPKG_HOST_TRIPLET=x64-mingw-dynamic `
  -DVCPKG_MANIFEST_MODE=ON
```

### 3. 程序提示找不到 `data/xxx.csv`

确认在项目根目录运行，或把数据文件放到对应位置。

### 4. 运行 `08_market_data_collection.exe --write-mysql` 报 `invalid utf8`

这是 MariaDB C Connector 在 Windows 上无法加载 MySQL 8 默认的 `caching_sha2_password` 插件导致的。错误信息是 ANSI 编码，`fmt` 格式化时会抛出 `invalid utf8`。

解决：把 MySQL 用户认证插件改为 `mysql_native_password`：

```sql
ALTER USER 'root'@'localhost' IDENTIFIED WITH mysql_native_password BY '你的密码';
FLUSH PRIVILEGES;
```

或新建专用用户：

```sql
CREATE USER 'quant'@'%' IDENTIFIED WITH mysql_native_password BY '你的密码';
GRANT ALL PRIVILEGES ON wucai_trade.* TO 'quant'@'%';
FLUSH PRIVILEGES;
```

如需删除该用户：

```sql
REVOKE ALL PRIVILEGES ON wucai_trade.* FROM 'quant'@'%';
DROP USER 'quant'@'%';
FLUSH PRIVILEGES;
```

详见 `week2(cpp)/README.md` 的 MySQL 配置说明。

---

## 构建示例（完整命令链）

```powershell
cd C:\Fan\ai-quant-cpp

# 配置
cmake -B build -S . -G "Ninja" `
  -DCMAKE_TOOLCHAIN_FILE=C:\Fan\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\Fan\ai-quant-cpp\build\vcpkg_installed\x64-mingw-dynamic `
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic `
  -DVCPKG_HOST_TRIPLET=x64-mingw-dynamic `
  -DVCPKG_MANIFEST_MODE=ON

# 编译
ninja -C build -j 4

# 设置 token（数据下载需要）
$env:TUSHARE_TOKEN = "你的token"

# 下载数据
./build/bin/Debug/01_tushare_download_data.exe

# 运行策略
./build/bin/Debug/02_macd_strategy_2025.exe
```
