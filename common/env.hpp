#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace quant::env {

using EnvMap = std::unordered_map<std::string, std::string>;
using Map = EnvMap; // backward-compatible alias

// Parse KEY=VALUE lines. Supports # comments and simple "..." / '...' quotes.
EnvMap parse_dotenv(const std::string& content);

// Load .env from path. Returns empty map if file not found / unreadable.
EnvMap load_dotenv(const std::string& path);

// Look for .env in candidates (first wins). Always includes process cwd and project root.
EnvMap find_and_load_dotenv(const std::vector<std::string>& candidates = {});

// Backward-compatible loaders used by week1/week2 code.
Map load(const std::string& path);
std::string get(const Map& env_map, const std::string& key);

// Helpers with defaults.
int get_int(const EnvMap& env, const std::string& key, int default_value);
double get_double(const EnvMap& env, const std::string& key, double default_value);
std::string get_string(const EnvMap& env, const std::string& key, const std::string& default_value);

} // namespace quant::env
