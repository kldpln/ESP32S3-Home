#include <string.h>
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_mac.h" // 包含 MACSTR 和 MAC2STR 宏的头文件

static const char *TAG = "WIFI_APSTA";

// 事件回调函数，用于处理STA连接状态
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "STA Disconnected. Trying to reconnect...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "STA Connected! Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap()// 配置 Wi-Fi 为 AP+STA 模式
{
    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    // 创建默认的事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 分别创建AP和STA的默认网络接口
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    // 初始化WiFi配置
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // 设置 AP 配置
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32_WEB", // 设置AP的SSID
            .ssid_len = strlen("ESP32_WEB"), // 设置SSID的长度
            .password = "12345678", // 设置AP的密码
            .max_connection = 4, // 稍微调大一点最大连接数
            .authmode = WIFI_AUTH_WPA_WPA2_PSK // 设置认证模式
        },
    };
    if (strlen((char *)ap_config.ap.password) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // 设置 STA 配置（尝试从 NVS 读取历史配置）
    wifi_config_t sta_config = {0};
    
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        size_t ssid_len = sizeof(sta_config.sta.ssid);
        size_t pass_len = sizeof(sta_config.sta.password);
        
        // 尝试获取 SSID 和 密码
        esp_err_t get_ssid_err = nvs_get_str(my_handle, "wifi_ssid", (char *)sta_config.sta.ssid, &ssid_len);
        esp_err_t get_pass_err = nvs_get_str(my_handle, "wifi_pass", (char *)sta_config.sta.password, &pass_len);
        
        if (get_ssid_err == ESP_OK && get_pass_err == ESP_OK) {
            ESP_LOGI(TAG, "从NVS读取到 Wi-Fi 配置, SSID: %s", sta_config.sta.ssid);
        } else {
            ESP_LOGW(TAG, "NVS中没有存历史 Wi-Fi 信息，等待网页配网");
        }
        nvs_close(my_handle);
    } else {
        ESP_LOGE(TAG, "无法打开 NVS 命名空间 'storage'");
    }

    // 设置WiFi模式为 AP + STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    
    // 使能配置
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    
    // 启动WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP+STA started.");
    ESP_LOGI(TAG, "AP SSID: %s, password: %s", ap_config.ap.ssid, ap_config.ap.password);
}