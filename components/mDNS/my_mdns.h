#ifndef MY_MDNS_H
#define MY_MDNS_H

#include "esp_err.h"

// 启动 mDNS 服务
esp_err_t start_mdns_service(void);

#endif // MY_MDNS_H