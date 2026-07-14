#pragma once

#include <string>

// MariaDB C Connector / MySQL C API
#include <mysql.h>

namespace quant::mysql {

struct Config {
    std::string host = "localhost";
    int port = 3306;
    std::string user;
    std::string password;
    std::string database;
};

class Client {
public:
    explicit Client(const Config& cfg);
    ~Client();

    bool connect();
    void close();
    bool connected() const;

    // 执行普通 SQL（如 CREATE TABLE、INSERT 等）
    bool execute(const std::string& sql);

    // 获取最后一次错误信息
    std::string last_error() const;

    MYSQL* raw() const { return conn_; }

private:
    Config cfg_;
    MYSQL* conn_ = nullptr;
};

} // namespace quant::mysql
