#include <string.h>
#include <esp_http_server.h>
#include "data_process.h"
#include "sys/time.h"
#include "time.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "ap.h"

/* 时间同步状态。 */
bool time_sync_done = false;

/* 报警阈值。 */
float g_alarm_threshold = 30.0;

/* 日志标签。 */
static const char *TAG = "WEBSERVER";

/* 由 objcopy 生成的嵌入资源。 */
extern const uint8_t _binary_index_html_start[];
extern const uint8_t _binary_index_html_end[];

/* Chart.js 资源。 */
extern const uint8_t _binary_chart_js_gz_start[];
extern const uint8_t _binary_chart_js_gz_end[];

/* 处理首页请求。 */
static esp_err_t index_handler(httpd_req_t *req)
{
    /* 设置响应类型。 */
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)_binary_index_html_start, _binary_index_html_end - _binary_index_html_start);
}

/* 生成通用数据 JSON。 */
static char* generate_data_json()
{
    /* 实时温湿度。 */
    bool reading_valid = get_current_reading_valid();
    float current_temp = get_current_temperature();
    float current_hum = get_current_humidity();

    /* 当日统计。 */
    float max_t_today, min_t_today, max_h_today, min_h_today;
    get_today_stats(&max_t_today, &min_t_today, &max_h_today, &min_h_today);

    /* 最近 7 天历史数据。 */
    DailyData history[7];
    get_weekly_history(history);

    /* 分配 JSON 缓冲区。 */
    char *json_response = malloc(2048);
    if (json_response == NULL) {
        return NULL;
    }
    char temp_field[32];
    char hum_field[32];
    if (reading_valid) {
        snprintf(temp_field, sizeof(temp_field), "\"%.1f\"", current_temp);
        snprintf(hum_field, sizeof(hum_field), "\"%.1f\"", current_hum);
    } else {
        snprintf(temp_field, sizeof(temp_field), "null");
        snprintf(hum_field, sizeof(hum_field), "null");
    }

    /* 当前数据。 */
    int offset = 0;
    offset += sprintf(json_response + offset, 
             "{\"reading_valid\": %s, \"temperature\": %s, \"humidity\": %s, "
             "\"max_temp_today\": \"%.1f\", \"min_temp_today\": \"%.1f\", "
             "\"max_hum_today\": \"%.1f\", \"min_hum_today\": \"%.1f\", "
             "\"alarmThreshold\": \"%.1f\", "
             "\"history\": [", 
             reading_valid ? "true" : "false",
             temp_field,
             hum_field,
             max_t_today, min_t_today, max_h_today, min_h_today,
             g_alarm_threshold);

    /* 历史数据。 */
    for (int i = 0; i < 7; i++) {
        if (history[i].valid) {
             offset += sprintf(json_response + offset, 
                "{\"day_ago\": %d, \"weekday\": %d, \"max_temp\": %.1f, \"min_temp\": %.1f, \"max_hum\": %.1f, \"min_hum\": %.1f},", 
                i + 1, history[i].weekday, history[i].max_temp, history[i].min_temp, history[i].max_hum, history[i].min_hum);
        } else {
             offset += sprintf(json_response + offset, "null,");
        }
    }
    
    /* 去掉末尾多余逗号。 */
    if (json_response[offset-1] == ',') {
        offset--;
    }

    /* 闭合 JSON。 */
    sprintf(json_response + offset, "]}");

    return json_response;
}

/* 处理数据请求。 */
static esp_err_t data_handler(httpd_req_t *req)
{
    /* 设置响应类型。 */
    httpd_resp_set_type(req, "application/json");

    char* json_response = generate_data_json();
    if(json_response == NULL) return ESP_FAIL;

    httpd_resp_send(req, json_response, strlen(json_response));

    free(json_response);
    
    return ESP_OK;   
}

/* WebSocket 消息处理。 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket 连接建立");
        return ESP_OK;
    }

    /* 读取 WebSocket 数据帧。 */
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "获取 WebSocket 数据帧长度失败: %d", ret);
        return ret;
    }

    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "获取 WebSocket 数据帧内容失败: %d", ret);
            free(buf);
            return ret;
        }
        
        ESP_LOGI(TAG, "收到 WebSocket 消息: %s", ws_pkt.payload);
        
        /* 收到 get 后立即推送最新数据。 */
        if(strcmp((char*)ws_pkt.payload, "get") == 0) {
            char* json_response = generate_data_json();
            if(json_response) {
                httpd_ws_frame_t ws_resp;
                memset(&ws_resp, 0, sizeof(httpd_ws_frame_t));
                ws_resp.payload = (uint8_t*)json_response;
                ws_resp.len = strlen(json_response);
                ws_resp.type = HTTPD_WS_TYPE_TEXT;
                
                httpd_ws_send_frame(req, &ws_resp);
                
                free(json_response);
            }
        }
        free(buf);
    }
    return ret;
}


/* 处理 Chart.js 请求。 */
static esp_err_t chart_handler(httpd_req_t *req)
{
    /* 设置响应类型。 */
    httpd_resp_set_type(req, "application/javascript");
    /* 设置内容编码。 */
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)_binary_chart_js_gz_start, _binary_chart_js_gz_end - _binary_chart_js_gz_start);
}

/* 处理时间同步请求。 */
static esp_err_t time_sync_handler(httpd_req_t *req)
{
    if (g_is_ntp_synced) {
        ESP_LOGI(TAG, "拒网页同步: 系统已连接网络并启用高质量 NTP 时间");
        httpd_resp_sendstr(req, "OK");
        return ESP_OK;
    }

    char time_str[32];
    int ret;
    int remaining = req->content_len;

    if (remaining >= sizeof(time_str))
    {
        remaining = sizeof(time_str) - 1;
    }

    ret = httpd_req_recv(req, time_str, remaining);
    if (ret <= 0)    
    {
        return ESP_FAIL;
    }
    time_str[ret] = '\0';

    long timestamp = atol(time_str);
    if (timestamp >0)
    {
        struct timeval tv = {
            .tv_sec = (time_t)timestamp,
            .tv_usec = 0
        };
        settimeofday(&tv, NULL);

        setenv("TZ", "CST-8", 1);
        tzset();

        time_sync_done = true;

        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "系统时间同步为: %s", strftime_buf);

        const char* response = "时间同步成功";
        return httpd_resp_send(req, response, strlen(response));
    }
    else
    {
        const char* response = "时间戳无效";
        return httpd_resp_send(req, response, strlen(response));
    }  
}

/* 处理 Wi-Fi 配置请求。 */
static esp_err_t wifi_config_handler(httpd_req_t *req)
{
    char buffer[200];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buffer)) {
        ESP_LOGE(TAG, "JSON 数据太大");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buffer, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buffer[ret] = '\0';

    ESP_LOGI(TAG, "收到配网数据: %s", buffer);

    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON 解析失败");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    cJSON *pwd_item = cJSON_GetObjectItem(root, "password");

    if (ssid_item && pwd_item && cJSON_IsString(ssid_item) && cJSON_IsString(pwd_item)) {
        const char *new_ssid = ssid_item->valuestring;
        const char *new_pwd = pwd_item->valuestring;
        ESP_LOGI(TAG, "准备连接 -> SSID: %s, Password: %s", new_ssid, new_pwd);
        /* 保存到 NVS。 */
        nvs_handle_t my_handle;
        esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
        if (err == ESP_OK) {
            nvs_set_str(my_handle, "wifi_ssid", new_ssid);
            nvs_set_str(my_handle, "wifi_pass", new_pwd);
            nvs_commit(my_handle);
            nvs_close(my_handle);
            ESP_LOGI(TAG, "Wi-Fi 信息已保存至 NVS");
        } else {
            ESP_LOGE(TAG, "NVS 打开失败，未保存 Wi-Fi 信息");
        }
        /* 应用新的 STA 配置。 */
        wifi_config_t sta_config = {0};
        strncpy((char *)sta_config.sta.ssid, new_ssid, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char *)sta_config.sta.password, new_pwd, sizeof(sta_config.sta.password) - 1);
        
        /* 断开当前连接并重新发起连接。 */
        esp_wifi_disconnect();
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        wifi_sta_reset_retry_and_connect();

        /* 等待获取 IP，超时 8 秒。 */
        esp_netif_t *netif_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info;
        int retry_count = 0;
        bool got_ip = false;
        
        while (retry_count < 80) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            if (netif_sta && esp_netif_get_ip_info(netif_sta, &ip_info) == ESP_OK) {
                if (ip_info.ip.addr != 0) {
                    got_ip = true;
                    break;
                }
            }
            retry_count++;
        }

        /* 返回连接结果。 */
        char response[128];
        if (got_ip) {
            snprintf(response, sizeof(response), "{\"status\":\"ok\", \"ip\":\"" IPSTR "\"}", IP2STR(&ip_info.ip));
        } else {
            snprintf(response, sizeof(response), "{\"status\":\"timeout\"}");
        }
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
    } else {
        ESP_LOGE(TAG, "JSON 字段不完整");
        httpd_resp_send_500(req);
    }

    cJSON_Delete(root);

    return ESP_OK;
}

/* 异步保存报警阈值。 */
static void save_alarm_task(void *pvParameters) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        char val_str[16];
        snprintf(val_str, sizeof(val_str), "%.1f", g_alarm_threshold);
        nvs_set_str(my_handle, "alarm_thresh", val_str);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "已异步保存报警阈值到 NVS: %s", val_str);
    }
    vTaskDelete(NULL);
}

/* 处理报警阈值设置请求。 */
static esp_err_t set_alarm_handler(httpd_req_t *req)
{
    char buffer[100];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buffer)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buffer, remaining);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buffer[ret] = '\0';

    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *threshold_item = cJSON_GetObjectItem(root, "threshold");
    if (threshold_item && cJSON_IsNumber(threshold_item)) {
        g_alarm_threshold = threshold_item->valuedouble;
        ESP_LOGI(TAG, "收到新报警阈值: %.1f", g_alarm_threshold);

        /* 异步保存到 NVS。 */
        xTaskCreate(save_alarm_task, "save_alarm_task", 3072, NULL, 4, NULL);

        const char* response = "{\"status\":\"ok\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, strlen(response));
    } else {
        httpd_resp_send_500(req);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* 启动 Web 服务器。 */
httpd_handle_t start_webserver(void)
{
    /* 从 NVS 加载报警阈值。 */
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        char val_str[16];
        size_t required_size = sizeof(val_str);
        if (nvs_get_str(my_handle, "alarm_thresh", val_str, &required_size) == ESP_OK) {
            g_alarm_threshold = atof(val_str);
            ESP_LOGI(TAG, "从 NVS 加载报警阈值: %.1f", g_alarm_threshold);
        }
        nvs_close(my_handle);
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* 允许回收空闲会话。 */
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 10;

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t data_uri = {
            .uri       = "/data",
            .method    = HTTP_GET,
            .handler   = data_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &data_uri);

        httpd_uri_t chart_uri = {
            .uri       = "/chart.js",
            .method    = HTTP_GET,
            .handler   = chart_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &chart_uri);

        httpd_uri_t time_sync_uri = {
            .uri       = "/sync_time",
            .method    = HTTP_POST,
            .handler   = time_sync_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &time_sync_uri);

        httpd_uri_t set_alarm_uri = {
            .uri       = "/set_alarm",
            .method    = HTTP_POST,
            .handler   = set_alarm_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &set_alarm_uri);

        httpd_uri_t ws_uri = {
            .uri        = "/ws",
            .method     = HTTP_GET,
            .handler    = ws_handler,
            .user_ctx   = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(server, &ws_uri);

        httpd_uri_t wifi_config_uri = {
            .uri       = "/wifi_config",
            .method    = HTTP_POST,
            .handler   = wifi_config_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &wifi_config_uri);
    }

    return server;
}