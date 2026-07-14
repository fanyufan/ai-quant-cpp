#include "mysql_client.hpp"

#include <string>

namespace quant::mysql {

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
    return std::string(mysql_error(conn_)) + " (" + std::to_string(mysql_errno(conn_)) + ")";
}

} // namespace quant::mysql
