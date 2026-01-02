#include <string.h> // 包含字符串处理函数库
#include "esp_event.h" // 包含ESP32事件处理功能库
#include "esp_log.h" // 包含ESP32日志功能库
#include "esp_netif.h" // 包含ESP32网络接口功能库
#include "nvs_flash.h" // 包含ESP32非易失性存储功能库
#include "esp_wifi.h" // 包含ESP32 Wi-Fi功能库
#include "web.h" // 包含webserver头文件
#include "ap.h" // 包含AP头文件
#include "dht11.h" // 包含DHT11头文件

// 主函数
void app_main()
{
    // 初始化非易失性存储器
    ESP_ERROR_CHECK(nvs_flash_init());
    // 初始化软AP
    wifi_init_softap();
    // 启动web服务器
    start_webserver();

    // 初始化 DHT11
    dht11_init();
    // 启动 DHT11 读取任务
    dht11_start_task();
}


