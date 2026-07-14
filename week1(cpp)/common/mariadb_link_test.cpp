// 验证 MariaDB C Connector 能否编译链接
#include <mysql.h>
#include <iostream>

int main() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        std::cerr << "mysql_init failed\n";
        return 1;
    }
    std::cout << "MariaDB C Connector init OK, client version: " << mysql_get_client_info() << "\n";
    mysql_close(conn);
    return 0;
}
