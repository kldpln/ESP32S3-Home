#include <string.h>
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"

void wifi_init_softap()//配置 Wi-Fi 为 AP 模式
{
    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    // 创建默认的事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // 创建默认的WiFi AP网络接口
    esp_netif_create_default_wifi_ap();

    // 初始化WiFi配置
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // 初始化WiFi
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 设置WiFi配置
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ESP32_WEB", // 设置AP的SSID
            .ssid_len = strlen("ESP32_WEB"), // 设置SSID的长度
            .password = "12345678", // 设置AP的密码
            .max_connection = 2, // 设置最大连接数
            .authmode = WIFI_AUTH_WPA_WPA2_PSK // 设置认证模式
        },
    };

    // 如果密码为空，则设置为开放模式
    if (strlen((char *)wifi_config.ap.password) == 0)
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    // 设置WiFi模式为AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    // 设置WiFi配置
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    // 启动WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    // 打印AP已启动的信息
    ESP_LOGI("AP", "Wi-Fi AP started. SSID:%s password:%s", "ESP32_WEB", "12345678");
}