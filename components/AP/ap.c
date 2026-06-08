#include <string.h>
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_mac.h"
#include "esp_sntp.h"
#include <time.h>

static const char *TAG = "WIFI_APSTA";
static const int MAX_STA_RETRY_COUNT = 5;
static int s_sta_retry_count = 0;
static bool s_sta_connect_enabled = false;
static bool s_sta_retry_blocked = false;

/* NTP 同步状态。 */
bool g_is_ntp_synced = false;

/* 重置 STA 重试状态并重新连接。 */
void wifi_sta_reset_retry_and_connect(void)
{
    s_sta_retry_count = 0;
    s_sta_connect_enabled = true;
    s_sta_retry_blocked = false;
    ESP_LOGI(TAG, "STA 重试状态已重置，正在使用新的 Wi-Fi 配置重新连接");
    esp_wifi_connect();
}

/* SNTP 时间同步回调。 */
void time_sync_notification_cb(struct timeval *tv)
{
    setenv("TZ", "CST-8", 1);
    tzset();

    time_t now = tv->tv_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);

    ESP_LOGI(TAG, "NTP 时间同步完成！系统时间已更新为：%s", strftime_buf);
    g_is_ntp_synced = true;
}

/* Wi-Fi 和 IP 事件处理函数。 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_sta_connect_enabled && !s_sta_retry_blocked) {
            ESP_LOGI(TAG, "STA 已启动，开始连接...");
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "STA 已启动，但当前没有有效的 Wi-Fi 配置，或重试已被阻止");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "STA 已断开连接");
        g_is_ntp_synced = false;

        if (!s_sta_connect_enabled) {
            ESP_LOGW(TAG, "当前还没有有效的 STA 配置，跳过重连");
            return;
        }

        if (s_sta_retry_blocked) {
            ESP_LOGW(TAG, "STA 重试已停止，等待新的 Wi-Fi 配置更新后再尝试");
            return;
        }

        if (s_sta_retry_count < MAX_STA_RETRY_COUNT) {
            s_sta_retry_count++;
            ESP_LOGI(TAG, "尝试重新连接...（%d/%d）", s_sta_retry_count, MAX_STA_RETRY_COUNT);
            esp_wifi_connect();
        } else {
            s_sta_retry_blocked = true;
            ESP_LOGW(TAG, "STA 重连已失败 %d 次，停止重试，等待新的 Wi-Fi 配置更新", MAX_STA_RETRY_COUNT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "STA 连接成功！获取到 IP：" IPSTR, IP2STR(&event->ip_info.ip));
        s_sta_retry_count = 0;
        s_sta_retry_blocked = false;
        s_sta_connect_enabled = true;

        if (!esp_sntp_enabled()) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "ntp.aliyun.com");
            esp_sntp_setservername(1, "pool.ntp.org");
            esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
            esp_sntp_init();
        }

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "有设备接入 AP：" MACSTR "，AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "有设备离开 AP：" MACSTR "，AID=%d", MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    /* AP 配置。 */
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32_WEB",
            .ssid_len = strlen("ESP32_WEB"),
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen((char *)ap_config.ap.password) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    /* 从 NVS 读取 STA 账号信息。 */
    wifi_config_t sta_config = {0};

    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        size_t ssid_len = sizeof(sta_config.sta.ssid);
        size_t pass_len = sizeof(sta_config.sta.password);

        esp_err_t get_ssid_err = nvs_get_str(my_handle, "wifi_ssid", (char *)sta_config.sta.ssid, &ssid_len);
        esp_err_t get_pass_err = nvs_get_str(my_handle, "wifi_pass", (char *)sta_config.sta.password, &pass_len);
        
        if (get_ssid_err == ESP_OK && get_pass_err == ESP_OK) {
            ESP_LOGI(TAG, "已从 NVS 读取到 Wi-Fi 配置，SSID：%s", sta_config.sta.ssid);
            s_sta_connect_enabled = true;
            s_sta_retry_blocked = false;
            s_sta_retry_count = 0;
        } else {
            ESP_LOGW(TAG, "NVS 中没有保存历史 Wi-Fi 信息，等待网页配网");
            s_sta_connect_enabled = false;
            s_sta_retry_blocked = false;
            s_sta_retry_count = 0;
        }
        nvs_close(my_handle);
    } else {
        ESP_LOGE(TAG, "无法打开 NVS 命名空间 'storage'");
        s_sta_connect_enabled = false;
        s_sta_retry_blocked = false;
        s_sta_retry_count = 0;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP+STA 已启动。");
    ESP_LOGI(TAG, "AP SSID：%s，密码：%s", ap_config.ap.ssid, ap_config.ap.password);
}