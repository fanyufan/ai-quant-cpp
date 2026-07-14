#include "mysql_client.hpp"

#include <string>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace quant::mysql {

namespace {

static bool is_valid_utf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t bytes = 0;
        if ((c & 0x80) == 0) {
            bytes = 1;
        } else if ((c & 0xE0) == 0xC0) {
            bytes = 2;
        } else if ((c & 0xF0) == 0xE0) {
            bytes = 3;
        } else if ((c & 0xF8) == 0xF0) {
            bytes = 4;
        } else {
            return false;
        }
        if (i + bytes > s.size()) {
            return false;
        }
        for (size_t j = 1; j < bytes; ++j) {
            if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += bytes;
    }
    return true;
}

#ifdef _WIN32
static std::string acp_to_utf8(const std::string& ansi) {
    if (ansi.empty()) return ansi;
    int wlen = MultiByteToWideChar(CP_ACP, 0, ansi.data(), static_cast<int>(ansi.size()), nullptr, 0);
    if (wlen <= 0) return ansi;
    std::wstring wstr(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, ansi.data(), static_cast<int>(ansi.size()), wstr.data(), wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return ansi;
    std::string utf8(ulen, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen, utf8.data(), ulen, nullptr, nullptr);
    return utf8;
}
#endif

static std::string to_printable_utf8(const std::string& raw) {
    if (is_valid_utf8(raw)) {
        return raw;
    }
#ifdef _WIN32
    return acp_to_utf8(raw);
#else
    // 非 Windows 平台直接按字节输出，避免 fmt 抛异常
    return raw;
#endif
}

} // namespace

Client::Client(const Config& cfg) : cfg_(cfg) {}

Client::~Client() {
    close();
}

bool Client::connect() {
    close();
    conn_ = mysql_init(nullptr);
    if (!conn_) {
        return false;
    }

    // 设置字符集
    mysql_options(conn_, MYSQL_SET_CHARSET_NAME, "utf8mb4");

#ifdef _WIN32
    // 让 MariaDB C Connector 从 libmariadb.dll 所在目录加载动态认证插件
    // （例如 caching_sha2_password.dll）
    HMODULE hmod = GetModuleHandleA("libmariadb.dll");
    if (hmod) {
        char path[MAX_PATH];
        if (GetModuleFileNameA(hmod, path, MAX_PATH)) {
            std::filesystem::path plugin_dir = std::filesystem::path(path).parent_path();
            mysql_options(conn_, MYSQL_PLUGIN_DIR, plugin_dir.string().c_str());
        }
    }
#endif

    if (!mysql_real_connect(conn_, cfg_.host.c_str(), cfg_.user.c_str(),
                            cfg_.password.c_str(), cfg_.database.c_str(),
                            cfg_.port, nullptr, 0)) {
        return false;
    }
    return true;
}

void Client::close() {
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

bool Client::connected() const {
    return conn_ != nullptr;
}

bool Client::execute(const std::string& sql) {
    if (!conn_) return false;
    return mysql_query(conn_, sql.c_str()) == 0;
}

std::string Client::last_error() const {
    if (!conn_) return "not connected";
    std::string raw = std::string(mysql_error(conn_)) + " (" + std::to_string(mysql_errno(conn_)) + ")";
    return to_printable_utf8(raw);
}

} // namespace quant::mysql
