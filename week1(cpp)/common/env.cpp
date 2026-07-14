#include "env.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace quant::env {

Map load(const std::string& path) {
    Map result;
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return result;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        // 去掉行尾 \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // 跳过空行和注释行
        if (line.empty() || line[0] == '#') {
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        // 去掉 key 首尾空白
        auto trim = [](std::string& s) {
            size_t start = 0;
            while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
            size_t end = s.size();
            while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
            s = s.substr(start, end - start);
        };
        trim(key);
        trim(value);
        // 去掉 value 首尾引号
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }
        if (!key.empty()) {
            result[key] = value;
        }
    }
    return result;
}

std::string get(const Map& env_map, const std::string& key) {
    auto it = env_map.find(key);
    if (it != env_map.end()) {
        return it->second;
    }
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : std::string();
}

} // namespace quant::env
