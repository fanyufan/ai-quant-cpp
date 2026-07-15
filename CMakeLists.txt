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

# Build bundled MariaDB Connector/C from source (minimal config)
set(WITH_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_CURL OFF CACHE BOOL "" FORCE)
set(WITH_SSL SCHANNEL CACHE STRING "" FORCE)
set(WITH_EXTERNAL_ZLIB ON CACHE BOOL "" FORCE)
set(WITH_ICONV OFF CACHE BOOL "" FORCE)
set(INSTALL_PLUGINDIR "lib/mariadb/plugin" CACHE STRING "" FORCE)
add_subdirectory(third_party/mariadb-connector-c-3.4.9 EXCLUDE_FROM_ALL)

# MySQL C client wrapper
add_library(quant_mysql STATIC
  "common/mysql_client.cpp"
  "common/backtest_data_mysql.cpp"
)

target_include_directories(quant_mysql PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/common"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mariadb-connector-c-3.4.9/include"
  "${CMAKE_CURRENT_BINARY_DIR}/third_party/mariadb-connector-c-3.4.9/include"
)

target_link_libraries(quant_mysql PUBLIC libmariadb)

# Common utility library
add_library(quant_utils STATIC
  "common/csv.cpp"
  "common/date_utils.cpp"
  "common/env.cpp"
  "common/indicators.cpp"
  "common/plotter.cpp"
  "common/tushare_client.cpp"
  "common/rank.cpp"
  "common/backtest.cpp"
  "common/backtest_data.cpp"
  "common/candlestick.cpp"
  "common/backtest_report.cpp"
  "common/backtest_plot.cpp"
  "common/weekly_bars.cpp"
  "common/backtest_config.cpp"
  "common/turtle_utils.cpp"
)

target_include_directories(quant_utils PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/common"
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

# Helper to add week2 executables with clean target names
function(add_week2_executable target_name source_subdir source_file)
  add_executable(${target_name} "week2(cpp)/${source_subdir}/${source_file}.cpp")
  target_link_libraries(${target_name} PRIVATE quant_utils)
  set_target_properties(${target_name} PROPERTIES
    RUNTIME_OUTPUT_NAME ${source_file}
  )
endfunction()

# Helper to add week2 executables that write to MySQL
function(add_week2_mysql_executable target_name source_subdir source_file)
  add_week2_executable(${target_name} ${source_subdir} ${source_file})
  target_link_libraries(${target_name} PRIVATE quant_mysql)

  # MariaDB 动态认证插件（如 caching_sha2_password）需要与 libmariadb.dll 一起部署
  add_dependencies(${target_name} caching_sha2_password)
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:libmariadb>"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Copying libmariadb.dll for ${target_name}"
  )
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:caching_sha2_password>"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Copying caching_sha2_password.dll for ${target_name}"
  )
endfunction()

# Helper to add week3 executables (backtest / TA-Lib scripts, may use MySQL data loader)
function(add_week3_executable target_name source_subdir source_file)
  add_executable(${target_name} "week3(cpp)/${source_subdir}/${source_file}.cpp")
  target_link_libraries(${target_name} PRIVATE quant_utils quant_mysql)
  set_target_properties(${target_name} PROPERTIES
    RUNTIME_OUTPUT_NAME ${source_file}
  )
  add_dependencies(${target_name} caching_sha2_password)
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:libmariadb>"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Copying libmariadb.dll for ${target_name}"
  )
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:caching_sha2_password>"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Copying caching_sha2_password.dll for ${target_name}"
  )
endfunction()

# Helper to add week4 executables (Turtle Trading, MySQL data loader)
function(add_week4_executable target_name source_subdir source_file)
  add_executable(${target_name} "week4(cpp)/${source_subdir}/${source_file}.cpp")
  target_link_libraries(${target_name} PRIVATE quant_utils quant_mysql)
  set_target_properties(${target_name} PROPERTIES
    RUNTIME_OUTPUT_NAME ${source_file}
  )
  add_dependencies(${target_name} caching_sha2_password)
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:libmariadb>"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Copying libmariadb.dll for ${target_name}"
  )
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:caching_sha2_password>"
      "$<TARGET_FILE_DIR:${target_name}>"
    COMMENT "Copying caching_sha2_password.dll for ${target_name}"
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

# week2(cpp) targets
# 3-Infrastructure_Building-20260211
add_week2_executable(w2_infra_01_daily_data_tushare "3-Infrastructure_Building-20260211/CASE-DataCollection" "01_daily_data_tushare")
add_week2_executable(w2_infra_02_minute_data_tushare "3-Infrastructure_Building-20260211/CASE-DataCollection" "02_minute_data_tushare")
add_week2_executable(w2_infra_03_financial_data_tushare "3-Infrastructure_Building-20260211/CASE-DataCollection" "03_financial_data_tushare")
add_week2_executable(w2_infra_04_multi_factor_filter1 "3-Infrastructure_Building-20260211/CASE-MultiFactorStockPicking" "04_multi_factor_filter1")
add_week2_executable(w2_infra_05_multi_factor_filter2 "3-Infrastructure_Building-20260211/CASE-MultiFactorStockPicking" "05_multi_factor_filter2")

# 4-Data_Acquisition_Cleaning-20260225
add_week2_executable(w2_data_06_key_catalyst_collection "4-Data_Acquisition_Cleaning-20260225/CASE-DataCollection" "06_key_catalyst_collection")
add_week2_mysql_executable(w2_data_08_market_data_collection "4-Data_Acquisition_Cleaning-20260225/CASE-DataCollection" "08_market_data_collection")
add_week2_mysql_executable(w2_data_09_financial_data_collection "4-Data_Acquisition_Cleaning-20260225/CASE-DataCollection" "09_financial_data_collection")
add_week2_mysql_executable(w2_data_10_macro_data_collection "4-Data_Acquisition_Cleaning-20260225/CASE-DataCollection" "10_macro_data_collection")

# week3(cpp) targets
# 5-Backtrader_Backtesting-20260228
add_week3_executable(w3_bt_01_dual_ma_strategy "5-Backtrader_Backtesting-20260228" "01_dual_ma_strategy")
add_week3_executable(w3_bt_02_macd_strategy "5-Backtrader_Backtesting-20260228" "02_macd_strategy")
add_week3_executable(w3_bt_03_rsi_strategy "5-Backtrader_Backtesting-20260228" "03_rsi_strategy")
add_week3_executable(w3_bt_04_bollinger_strategy "5-Backtrader_Backtesting-20260228" "04_bollinger_strategy")
add_week3_executable(w3_bt_05_bias_strategy "5-Backtrader_Backtesting-20260228" "05_bias_strategy")
add_week3_executable(w3_bt_06_momentum_strategy "5-Backtrader_Backtesting-20260228" "06_momentum_strategy")

# 6-Talib_Technical_Indicators-20260304
add_week3_executable(w3_ta_01_indicator_comparison "6-Talib_Technical_Indicators-20260304" "01_indicator_comparison")
add_week3_executable(w3_ta_02_indicator_basics "6-Talib_Technical_Indicators-20260304" "02_indicator_basics")
add_week3_executable(w3_ta_03_candlestick_patterns "6-Talib_Technical_Indicators-20260304" "03_candlestick_patterns")
add_week3_executable(w3_ta_04_rsi_cross_confirm "6-Talib_Technical_Indicators-20260304" "04_rsi_cross_confirm")
add_week3_executable(w3_ta_05_macd_volume_confirm "6-Talib_Technical_Indicators-20260304" "05_macd_volume_confirm")
add_week3_executable(w3_ta_06_macd_trailing_stop "6-Talib_Technical_Indicators-20260304" "06_macd_trailing_stop")
add_week3_executable(w3_ta_07_bollinger_middle_band_stop "6-Talib_Technical_Indicators-20260304" "07_bollinger_middle_band_stop")
add_week3_executable(w3_ta_08_adaptive_strategy "6-Talib_Technical_Indicators-20260304" "08_adaptive_strategy")
add_week3_executable(w3_ta_09_pattern_scanner "6-Talib_Technical_Indicators-20260304" "09_pattern_scanner")

# week4(cpp) targets
# 8-Turtle_Trading-20260311
add_week4_executable(w4_turtle_01_classic "8-Turtle_Trading-20260311" "01_classic_turtle")
add_week4_executable(w4_turtle_02_adx "8-Turtle_Trading-20260311" "02_adx_turtle")
add_week4_executable(w4_turtle_03_multi_tf "8-Turtle_Trading-20260311" "03_multi_tf_turtle")
add_week4_executable(w4_turtle_04_ml "8-Turtle_Trading-20260311" "04_ml_turtle")

# MariaDB C Connector 链接测试
add_executable(mariadb_link_test "common/mariadb_link_test.cpp")
target_include_directories(mariadb_link_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mariadb-connector-c-3.4.9/include"
  "${CMAKE_CURRENT_BINARY_DIR}/third_party/mariadb-connector-c-3.4.9/include"
)
target_link_libraries(mariadb_link_test PRIVATE libmariadb)
set_target_properties(mariadb_link_test PROPERTIES RUNTIME_OUTPUT_NAME "mariadb_link_test")

# 运行时需要 libmariadb.dll 在可执行文件同目录
add_custom_command(TARGET mariadb_link_test POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:libmariadb>"
    "$<TARGET_FILE_DIR:mariadb_link_test>"
  COMMENT "Copying libmariadb.dll to output directory"
)
