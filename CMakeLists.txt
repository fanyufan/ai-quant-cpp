cmake_minimum_required(VERSION 3.20)
project(ai-quant-cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Debug
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Output executables to build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release)

# Warnings
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# vcpkg dependencies
find_package(Matplot++ CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

# Common utility library
add_library(quant_utils STATIC
  "week1(cpp)/common/csv.cpp"
  "week1(cpp)/common/date_utils.cpp"
  "week1(cpp)/common/indicators.cpp"
  "week1(cpp)/common/plotter.cpp"
  "week1(cpp)/common/tushare_client.cpp"
)

target_include_directories(quant_utils PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/week1(cpp)/common"
)

target_link_libraries(quant_utils PUBLIC
  Matplot++::matplot
  nlohmann_json::nlohmann_json
  cpr::cpr
  fmt::fmt
)

# Helper to add week1 executables with clean target names
function(add_week1_executable target_name source_subdir source_file)
  add_executable(${target_name} "week1(cpp)/${source_subdir}/${source_file}.cpp")
  target_link_libraries(${target_name} PRIVATE quant_utils)
  set_target_properties(${target_name} PROPERTIES
    RUNTIME_OUTPUT_NAME ${source_file}
  )
endfunction()

# 1-AI 量化交易-20260204
add_week1_executable(ai_01_tushare_download_data "1-AI_Quant_Trading-20260204" "01_tushare_download_data")
add_week1_executable(ai_02_macd_strategy_2025 "1-AI_Quant_Trading-20260204" "02_macd_strategy_2025")
add_week1_executable(ai_03_grid_strategy_2025 "1-AI_Quant_Trading-20260204" "03_grid_strategy_2025")

# 2-Finance_Basics-20260207
add_week1_executable(fb_01_kline_volume "2-Finance_Basics-20260207" "01_kline_volume")
add_week1_executable(fb_02_moutai_financial_indicators "2-Finance_Basics-20260207" "02_moutai_financial_indicators")
add_week1_executable(fb_03_graham_pb_stock_picker "2-Finance_Basics-20260207" "03_graham_pb_stock_picker")
add_week1_executable(fb_04_fundamental_stock_picker "2-Finance_Basics-20260207" "04_fundamental_stock_picker")
add_week1_executable(fb_05_moutai_ma_signals "2-Finance_Basics-20260207" "05_moutai_ma_signals")
add_week1_executable(fb_06_moutai_macd_signals "2-Finance_Basics-20260207" "06_moutai_macd_signals")
add_week1_executable(fb_07_moutai_rsi "2-Finance_Basics-20260207" "07_moutai_rsi")
add_week1_executable(fb_08_moutai_atr "2-Finance_Basics-20260207" "08_moutai_atr")
add_week1_executable(fb_09_moutai_indicator_dashboard "2-Finance_Basics-20260207" "09_moutai_indicator_dashboard")
add_week1_executable(fb_10_tushare_financial_data_download "2-Finance_Basics-20260207" "10_tushare_financial_data_download")
