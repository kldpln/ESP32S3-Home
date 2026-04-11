#include "my_mdns.h"
#include "mdns.h"
#include "esp_log.h"

static const char *TAG = "mDNS_SVC";

esp_err_t start_mdns_service(void)
{
    // 初始化 mDNS 底层服务
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGE(TAG, "mDNS 初始化失败: %d", err);
        return err;
    }

    // 设置主机名 (Hostname)
    // 在电脑浏览器输入 "http://esp.local" 直接访问，不用敲 IP
    mdns_hostname_set("esp");

    // 设置实例名 (Instance Name)
    mdns_instance_name_set("Legadema Smart Home");

    // 添加一个公开服务
      mdns_service_add("Legadema WebServer", "_legadema", "_tcp", 80, NULL, 0);
    // "_http"表示网页服务, "_tcp"表示TCP协议， "80"是你的 WebServer 默认端口
    mdns_service_add("Legadema WebServer", "_http", "_tcp", 80, NULL, 0);

    ESP_LOGI(TAG, "mDNS 服务已成功开启！");
    return ESP_OK;
}