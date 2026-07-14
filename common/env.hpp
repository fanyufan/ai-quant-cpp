#pragma once

#include <string>
#include <unordered_map>

namespace quant::env {

using Map = std::unordered_map<std::string, std::string>;

// 从 .env 文件加载 KEY=VALUE 配置，忽略注释行（#）和空行
Map load(const std::string& path);

// 优先从 env_map 取值，否则从系统环境变量取值
std::string get(const Map& env_map, const std::string& key);

} // namespace quant::env
