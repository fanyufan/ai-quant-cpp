// 对应 Python: 7-关键催化剂采集.py
// 说明：本版本将 MySQL 写入替换为 CSV + SQL 文件输出，不依赖数据库。
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <cpr/cpr.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
namespace fs = std::filesystem;
using json = nlohmann::json;

static std::tm parse_ymd(const std::string& s) {
    std::tm tm = {};
    int y = 0, m = 0, d = 0;
    if (sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) == 3) {
        tm.tm_year = y - 1900;
        tm.tm_mon = m - 1;
        tm.tm_mday = d;
    } else if (sscanf(s.c_str(), "%4d%2d%2d", &y, &m, &d) == 3) {
        tm.tm_year = y - 1900;
        tm.tm_mon = m - 1;
        tm.tm_mday = d;
    }
    return tm;
}

static std::string format_ymd(const std::tm& tm) {
    return fmt::format("{:04d}-{:02d}-{:02d}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

static std::string add_days(const std::string& date_str, int days) {
    std::tm tm = parse_ymd(date_str);
    tm.tm_mday += days;
    std::mktime(&tm);
    return format_ymd(tm);
}

static int days_diff(const std::string& a, const std::string& b) {
    std::tm ta = parse_ymd(a);
    std::tm tb = parse_ymd(b);
    std::time_t t1 = std::mktime(&ta);
    std::time_t t2 = std::mktime(&tb);
    if (t1 == -1 || t2 == -1) return std::numeric_limits<int>::max();
    return static_cast<int>(std::difftime(t1, t2) / 86400.0);
}

static std::string today_string() {
    std::time_t now = std::time(nullptr);
    std::tm* lt = std::localtime(&now);
    return fmt::format("{:04d}-{:02d}-{:02d}", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
}

static std::string normalize_title(const std::string& t) {
    static const std::regex re(R"([\s/\-—()（）])");
    return std::regex_replace(t, re, "");
}

struct PromptsConfig {
    std::string search_catalysts;
    std::string generate_prompts;
};

static size_t leading_spaces(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return i;
}

static std::string trim_right(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
    return s;
}

static std::string extract_block(const std::vector<std::string>& lines, const std::string& key) {
    size_t key_line = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        size_t indent = leading_spaces(lines[i]);
        if (indent >= lines[i].size()) continue;
        std::string stripped = lines[i].substr(indent);
        if (stripped.rfind(key, 0) == 0) {
            key_line = i;
            break;
        }
    }
    if (key_line == lines.size()) return "";

    size_t base_indent = leading_spaces(lines[key_line]);
    std::vector<std::string> block;
    for (size_t i = key_line + 1; i < lines.size(); ++i) {
        if (lines[i].empty()) {
            block.push_back("");
            continue;
        }
        if (leading_spaces(lines[i]) <= base_indent) break;
        block.push_back(lines[i]);
    }

    size_t min_indent = std::numeric_limits<size_t>::max();
    for (const auto& b : block) {
        if (!b.empty()) min_indent = std::min(min_indent, leading_spaces(b));
    }
    if (min_indent == std::numeric_limits<size_t>::max()) min_indent = 0;

    std::string out;
    for (auto& b : block) {
        if (!b.empty()) {
            if (leading_spaces(b) >= min_indent) out += b.substr(min_indent);
        }
        out += "\n";
    }
    out = trim_right(out);
    return out;
}

static PromptsConfig load_prompts_config(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error(fmt::format("无法打开 prompts 文件: {}", path));
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(line);
    PromptsConfig cfg;
    cfg.search_catalysts = extract_block(lines, "search_catalysts:");
    cfg.generate_prompts = extract_block(lines, "generate_prompts:");
    return cfg;
}

static std::string json_to_string(const json& j) {
    if (j.is_null()) return "";
    if (j.is_string()) return j.get<std::string>();
    return j.dump();
}

static json parse_json_array(const std::string& content) {
    size_t start = content.find('[');
    size_t end = content.rfind(']');
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        return json::array();
    }
    try {
        return json::parse(content.substr(start, end - start + 1));
    } catch (...) {
        return json::array();
    }
}

static std::string call_qwen(const std::string& api_key, const std::string& prompt, bool enable_search) {
    json body;
    body["model"] = "qwen-max";
    body["messages"] = json::array({{{"role", "user"}, {"content", prompt}}});
    if (enable_search) {
        body["enable_search"] = true;
        body["search_options"] = {{"forced_search", true}};
    }

    auto response = cpr::Post(
        cpr::Url{"https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"},
        cpr::Header{{"Content-Type", "application/json"},
                    {"Authorization", fmt::format("Bearer {}", api_key)}},
        cpr::Body{body.dump()});

    if (response.status_code != 200) {
        throw std::runtime_error(fmt::format("HTTP error {}: {}", response.status_code, response.text));
    }

    json resp = json::parse(response.text);
    if (!resp.contains("choices") || resp["choices"].empty()) {
        throw std::runtime_error("Invalid response: no choices");
    }
    return json_to_string(resp["choices"][0]["message"]["content"]);
}

static json search_catalysts(const std::string& api_key, const std::string& prompts_path) {
    auto cfg = load_prompts_config(prompts_path);
    std::string prompt_tpl = cfg.search_catalysts;

    std::string today = today_string();
    std::string end_date = add_days(today, 180);
    std::string prompt = prompt_tpl;
    size_t pos = prompt.find("{start_date}");
    if (pos != std::string::npos) prompt.replace(pos, std::string("{start_date}").size(), today);
    pos = prompt.find("{end_date}");
    if (pos != std::string::npos) prompt.replace(pos, std::string("{end_date}").size(), end_date);

    fmt::print("搜索范围: {} ~ {}\n", today, end_date);
    fmt::print("调用 Qwen Max 联网搜索...\n");

    std::string content = call_qwen(api_key, prompt, true);
    fmt::print("  原始响应长度: {} 字符\n", content.size());

    json events = parse_json_array(content);
    if (events.empty()) {
        fmt::print("  未找到 JSON 数组，原始内容:\n{}\n", content.substr(0, 500));
        return json::array();
    }
    fmt::print("  解析到 {} 个事件\n", events.size());
    return events;
}

static std::unordered_map<std::string, std::string> generate_prompts(
    const std::string& api_key, const std::string& prompts_path, const json& events) {

    auto cfg = load_prompts_config(prompts_path);
    std::string prompt_tpl = cfg.generate_prompts;

    std::vector<json> briefs;
    for (const auto& e : events) {
        json b;
        b["date"] = e.value("date", "");
        b["title"] = e.value("title", "");
        b["country"] = e.value("country", "");
        briefs.push_back(b);
    }

    std::unordered_map<std::string, std::string> result;
    for (size_t i = 0; i < briefs.size(); i += 20) {
        size_t end = std::min(i + 20, briefs.size());
        json batch = json::array();
        for (size_t j = i; j < end; ++j) batch.push_back(briefs[j]);

        std::string events_json = batch.dump(-1, ' ', false, json::error_handler_t::replace);
        std::string prompt = prompt_tpl;
        size_t pos = prompt.find("{events_json}");
        if (pos != std::string::npos) prompt.replace(pos, std::string("{events_json}").size(), events_json);

        fmt::print("  生成prompt第 {} 批 ({} 个事件)...\n", i / 20 + 1, end - i);
        std::string content = call_qwen(api_key, prompt, false);
        json arr = parse_json_array(content);
        for (const auto& r : arr) {
            std::string title = r.value("title", "");
            std::string prompt_text = r.value("prompt", "");
            if (!title.empty() && !prompt_text.empty()) {
                result[title] = prompt_text;
            }
        }
    }
    fmt::print("  共生成 {} 个prompt\n", result.size());
    return result;
}

static void save_events_csv(const std::string& path,
                            const json& events,
                            const std::unordered_map<std::string, std::string>& prompts_map) {
    std::vector<std::string> headers = {"event_date", "event_time", "title", "country",
                                        "category", "importance", "source", "ai_prompt"};
    std::vector<std::vector<std::string>> rows;

    for (const auto& e : events) {
        std::string date_str = e.value("date", "");
        std::string title = e.value("title", "");
        if (date_str.empty() || title.empty()) continue;

        std::string country = e.value("country", "中国");
        std::string category = e.value("category", "policy");
        int importance = e.value("importance", 2);
        importance = std::max(2, std::min(3, importance));

        auto it = prompts_map.find(title);
        std::string ai_prompt = it != prompts_map.end() ? it->second : "";

        std::vector<std::string> row = {date_str, "", title, country, category,
                                        std::to_string(importance), "qwen_search", ai_prompt};
        rows.push_back(std::move(row));
    }

    fs::create_directories(fs::path(path).parent_path());

    std::ofstream ofs(path, std::ios::binary);
    // UTF-8 BOM
    ofs.write("\xEF\xBB\xBF", 3);
    for (size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) ofs << ",";
        ofs << headers[i];
    }
    ofs << "\n";
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            if (i > 0) ofs << ",";
            // 简单 CSV 转义
            bool need_quote = row[i].find(',') != std::string::npos ||
                              row[i].find('"') != std::string::npos ||
                              row[i].find('\n') != std::string::npos;
            if (need_quote) {
                ofs << '"';
                for (char c : row[i]) {
                    if (c == '"') ofs << '"';
                    ofs << c;
                }
                ofs << '"';
            } else {
                ofs << row[i];
            }
        }
        ofs << "\n";
    }
}

static void save_events_sql(const std::string& path,
                            const json& events,
                            const std::unordered_map<std::string, std::string>& prompts_map) {
    std::ofstream ofs(path, std::ios::binary);
    ofs.write("\xEF\xBB\xBF", 3);
    ofs << "-- 关键催化剂事件 SQL (由 06_key_catalyst_collection.cpp 生成)\n";
    ofs << "-- 表: trade_calendar_event\n\n";

    std::string sql = R"(
INSERT INTO trade_calendar_event
(event_date, event_time, title, country, category,
 importance, source, ai_prompt)
VALUES (?, ?, ?, ?, ?, ?, ?, ?)
ON DUPLICATE KEY UPDATE
importance = GREATEST(importance, VALUES(importance)),
category = VALUES(category),
source = VALUES(source),
ai_prompt = COALESCE(VALUES(ai_prompt), ai_prompt);
)";

    for (const auto& e : events) {
        std::string date_str = e.value("date", "");
        std::string title = e.value("title", "");
        if (date_str.empty() || title.empty()) continue;

        std::string country = e.value("country", "中国");
        std::string category = e.value("category", "policy");
        int importance = e.value("importance", 2);
        importance = std::max(2, std::min(3, importance));

        auto it = prompts_map.find(title);
        std::string ai_prompt = it != prompts_map.end() ? it->second : "";

        // 简单转义单引号
        auto escape = [](std::string s) {
            std::string out;
            for (char c : s) {
                if (c == '\'') out += "''";
                else out += c;
            }
            return out;
        };

        ofs << "INSERT INTO trade_calendar_event "
               "(event_date, event_time, title, country, category, importance, source, ai_prompt) "
               "VALUES ('" << escape(date_str) << "', NULL, '" << escape(title) << "', '"
            << escape(country) << "', '" << escape(category) << "', " << importance
            << ", 'qwen_search', ";
        if (ai_prompt.empty()) {
            ofs << "NULL";
        } else {
            ofs << "'" << escape(ai_prompt) << "'";
        }
        ofs << ") ON DUPLICATE KEY UPDATE importance = GREATEST(importance, VALUES(importance)), "
               "category = VALUES(category), source = VALUES(source), "
               "ai_prompt = COALESCE(VALUES(ai_prompt), ai_prompt);\n";
    }
}

int main(int argc, char* argv[]) {
    std::string prompts_path = "week2(cpp)/4-Data_Acquisition_Cleaning-20260225/CASE-DataCollection/prompts.yaml";
    std::string output_dir = "data";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--prompts" || arg == "-p") && i + 1 < argc) {
            prompts_path = argv[++i];
        } else if ((arg == "--output-dir" || arg == "-o") && i + 1 < argc) {
            output_dir = argv[++i];
        }
    }

    const char* api_key_env = std::getenv("DASHSCOPE_API_KEY");
    if (!api_key_env || std::string(api_key_env).empty()) {
        fmt::print("错误：未找到环境变量 DASHSCOPE_API_KEY\n");
        fmt::print("请设置环境变量：set DASHSCOPE_API_KEY=your_key\n");
        return 1;
    }
    std::string api_key = api_key_env;

    if (!fs::exists(prompts_path)) {
        fmt::print("错误：未找到 prompts 文件 {}\n", prompts_path);
        fmt::print("请使用 --prompts 指定正确路径\n");
        return 1;
    }

    fmt::print("{:=<60}\n", "");
    fmt::print("关键催化剂事件采集 (Qwen Max 联网搜索)\n");
    fmt::print("{:=<60}\n", "");

    try {
        json events = search_catalysts(api_key, prompts_path);
        if (events.empty()) {
            fmt::print("\n未获取到事件\n");
            return 0;
        }

        // 事件预览
        fmt::print("\n事件预览 ({} 个):\n", events.size());
        for (const auto& e : events) {
            int imp = e.value("importance", 2);
            std::string stars(imp, '*');
            fmt::print("  [{}] {} {} {}\n", stars, e.value("date", "?"),
                       e.value("country", "?"), e.value("title", "?"));
        }

        // 生成 prompt
        fmt::print("\n生成事件提问prompt...\n");
        auto prompts_map = generate_prompts(api_key, prompts_path, events);

        // 去重：同一归一化标题在 5 天内只保留一个（日期相差 1~5 天）
        json deduped = json::array();
        std::unordered_map<std::string, std::vector<std::string>> seen;
        for (const auto& e : events) {
            std::string date_str = e.value("date", "");
            std::string title = e.value("title", "");
            if (date_str.empty() || title.empty()) continue;

            std::string norm = normalize_title(title);
            bool skip = false;
            for (const auto& d : seen[norm]) {
                int diff = std::abs(days_diff(date_str, d));
                if (diff > 0 && diff <= 5) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;
            seen[norm].push_back(date_str);
            deduped.push_back(e);
        }

        fs::create_directories(output_dir);
        std::string csv_path = fmt::format("{}/catalyst_events.csv", output_dir);
        std::string sql_path = fmt::format("{}/catalyst_events.sql", output_dir);

        save_events_csv(csv_path, deduped, prompts_map);
        save_events_sql(sql_path, deduped, prompts_map);

        fmt::print("\n写入/更新 {} 条催化剂事件\n", deduped.size());
        fmt::print("CSV: {}\n", csv_path);
        fmt::print("SQL: {}\n", sql_path);

        // 按 importance 统计
        std::unordered_map<int, int> stats;
        for (const auto& e : deduped) {
            int imp = e.value("importance", 2);
            ++stats[imp];
        }
        fmt::print("\nqwen_search 来源统计:\n");
        std::vector<int> keys;
        for (const auto& kv : stats) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end(), std::greater<int>());
        for (int k : keys) {
            fmt::print("  {}星: {} 条\n", k, stats[k]);
        }

        fmt::print("\n{:=<60}\n", "");
        fmt::print("催化剂采集完成!\n");
        fmt::print("{:=<60}\n", "");

        return 0;
    } catch (const std::exception& e) {
        fmt::print("运行过程中发生错误：{}\n", e.what());
        return 1;
    }
}
