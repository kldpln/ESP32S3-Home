#include "my_mdns.h"
#include "mdns.h"
#include "esp_log.h"

static const char *TAG = "mDNS_SVC";

esp_err_t start_mdns_service(void)
{
  /* 初始化 mDNS 服务。 */
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGE(TAG, "mDNS 初始化失败: %d", err);
        return err;
    }

  /* 设置主机名。 */
    mdns_hostname_set("esp");

  /* 设置实例名。 */
    mdns_instance_name_set("Legadema Smart Home");

  /* 发布 Web 服务。 */
  mdns_service_add("Legadema WebServer", "_legadema", "_tcp", 80, NULL, 0);
    mdns_service_add("Legadema WebServer", "_http", "_tcp", 80, NULL, 0);

    ESP_LOGI(TAG, "mDNS 服务已成功开启！");
    return ESP_OK;
}