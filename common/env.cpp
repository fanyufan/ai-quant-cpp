#include "env.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace quant::env {

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string unquote(const std::string& s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                          (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

} // namespace

EnvMap parse_dotenv(const std::string& content) {
    EnvMap result;
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = unquote(trim(line.substr(eq + 1)));
        if (!key.empty()) result[key] = value;
    }
    return result;
}

EnvMap load_dotenv(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return parse_dotenv(oss.str());
}

EnvMap find_and_load_dotenv(const std::vector<std::string>& candidates) {
    std::vector<std::string> paths = candidates;
    // current working directory
    try {
        paths.push_back((fs::current_path() / ".env").string());
    } catch (...) {
    }
    // project root (relative to this source file location ../.env)
    try {
        fs::path root = fs::path(__FILE__).parent_path().parent_path() / ".env";
        paths.push_back(root.string());
    } catch (...) {
    }

    for (const auto& p : paths) {
        auto env = load_dotenv(p);
        if (!env.empty()) return env;
    }
    return {};
}

// Backward-compatible API
Map load(const std::string& path) {
    Map result;
    std::ifstream ifs(path);
    if (!ifs.is_open()) return result;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = trim(line.substr(0, pos));
        std::string value = unquote(trim(line.substr(pos + 1)));
        if (!key.empty()) result[key] = value;
    }
    return result;
}

std::string get(const Map& env_map, const std::string& key) {
    auto it = env_map.find(key);
    if (it != env_map.end()) return it->second;
    const char* val = std::getenv(key.c_str());
    return val ? std::string(val) : std::string();
}

int get_int(const EnvMap& env, const std::string& key, int default_value) {
    auto it = env.find(key);
    if (it == env.end()) return default_value;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return default_value;
    }
}

double get_double(const EnvMap& env, const std::string& key, double default_value) {
    auto it = env.find(key);
    if (it == env.end()) return default_value;
    try {
        return std::stod(it->second);
    } catch (...) {
        return default_value;
    }
}

std::string get_string(const EnvMap& env, const std::string& key, const std::string& default_value) {
    auto it = env.find(key);
    if (it == env.end()) return default_value;
    return it->second;
}

} // namespace quant::env
