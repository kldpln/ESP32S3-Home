#ifndef WEB_H
#define WEB_H

#include "esp_http_server.h"

// 启动web服务器的函数声明
httpd_handle_t start_webserver(void);

#endif // WEB_H