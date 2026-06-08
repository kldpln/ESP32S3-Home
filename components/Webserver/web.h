#ifndef WEB_H
#define WEB_H

#include "esp_http_server.h"

/* 启动 Web 服务器。 */
httpd_handle_t start_webserver(void);

#endif