#include <string.h>
#include <esp_http_server.h>
#include "data_process.h"
#include "sys/time.h"
#include "time.h"
#include "esp_log.h"
#include "cJSON.h" // 引入 cJSON 库来解析网页发来的账号密码
#include "esp_wifi.h" // 引入 WiFi 库以应用新的配置
#include "nvs_flash.h" // 引入 NVS 保存 Wi-Fi 账号密码
#include "nvs.h"
#include "ap.h" // 引入 AP 模块获取 NTP 状态

// 定义时间同步标志位在开头
bool time_sync_done = false;

// 声明全局报警阈值，默认 30.0
float g_alarm_threshold = 30.0;

//声明一下静态的TAG
static const char *TAG = "WEBSERVER";

// 嵌入资源（命名由 objcopy 自动生成）
extern const uint8_t _binary_index_html_start[];
extern const uint8_t _binary_index_html_end[];

//嵌入 chart.js 库
extern const uint8_t _binary_chart_js_gz_start[];
extern const uint8_t _binary_chart_js_gz_end[];

// 处理index.html文件请求
static esp_err_t index_handler(httpd_req_t *req)
{
    // 设置响应类型为text/html
    httpd_resp_set_type(req, "text/html");
    // 发送动态 HTML
    return httpd_resp_send(req, (const char *)_binary_index_html_start, _binary_index_html_end - _binary_index_html_start);
}

// 提取生成 JSON 数据的通用逻辑，让 HTTP /data 接口和 WebSocket 接口都能复用
static char* generate_data_json()
{
    // 获取实时温湿度数据
    int temp_int = get_temperature_int();
    int temp_dec = get_temperature_dec();
    int hum_int = get_humidity_int();
    int hum_dec = get_humidity_dec();

    // 获取今日统计数据
    float max_t_today, min_t_today, max_h_today, min_h_today;
    get_today_stats(&max_t_today, &min_t_today, &max_h_today, &min_h_today);

    //获取七天历史数据
    DailyData history[7];
    get_weekly_history(history);

    // 生成 JSON
    char *json_response = malloc(2048);
    if (json_response == NULL) {
        return NULL;
    }
    //今日数据
    int offset = 0;
    offset += sprintf(json_response + offset, 
             "{\"temperature\": \"%d.%d\", \"humidity\": \"%d.%d\", "
             "\"max_temp_today\": \"%.1f\", \"min_temp_today\": \"%.1f\", "
             "\"max_hum_today\": \"%.1f\", \"min_hum_today\": \"%.1f\", "
             "\"alarmThreshold\": \"%.1f\", "
             "\"history\": [", 
             temp_int, temp_dec, hum_int, hum_dec, 
             max_t_today, min_t_today, max_h_today, min_h_today,
             g_alarm_threshold);

    // 循环写入历史数组
    for (int i = 0; i < 7; i++) {
        // 如果数据无效，就填 null 或者 0，前端判断 valid 字段
        if (history[i].valid) {
             offset += sprintf(json_response + offset, 
                "{\"day_ago\": %d, \"weekday\": %d, \"max_temp\": %.1f, \"min_temp\": %.1f, \"max_hum\": %.1f, \"min_hum\": %.1f},", 
                i + 1, history[i].weekday, history[i].max_temp, history[i].min_temp, history[i].max_hum, history[i].min_hum);
        } else {
             // 无效数据传个标志
             offset += sprintf(json_response + offset, "null,");
        }
    }
    
    // 去掉最后一个多余的逗号 (如果数组不为空)
    if (json_response[offset-1] == ',') {
        offset--;
    }

    // 闭合数组和 JSON 对象
    sprintf(json_response + offset, "]}");

    return json_response;
}

// 处理数据请求，返回 JSON（保留旧的 HTTP 轮询接口，平滑过渡）
static esp_err_t data_handler(httpd_req_t *req)
{
    // 设置响应类型为application/json
    httpd_resp_set_type(req, "application/json");

    char* json_response = generate_data_json();
    if(json_response == NULL) return ESP_FAIL;

    // 发送
    httpd_resp_send(req, json_response, strlen(json_response));
    
    // 释放内存
    free(json_response);
    
    return ESP_OK;   
}

// WebSocket 消息处理程序
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        // HTTP 升级到 WebSocket 的初次握手请求
        ESP_LOGI(TAG, "WebSocket 连接建立");
        return ESP_OK;
    }

    // 处理网页发来的 WebSocket 数据帧
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
        
        // 打印前端发来的内容（比如 "get" 命令）
        ESP_LOGI(TAG, "收到 WebSocket 消息: %s", ws_pkt.payload);
        
        // 当收到 "get" 请求时，我们立刻生成完整数据，封装成 WebSocket 专属帧推给网页
        if(strcmp((char*)ws_pkt.payload, "get") == 0) {
            char* json_response = generate_data_json();
            if(json_response) {
                httpd_ws_frame_t ws_resp;
                memset(&ws_resp, 0, sizeof(httpd_ws_frame_t));
                ws_resp.payload = (uint8_t*)json_response;
                ws_resp.len = strlen(json_response);
                ws_resp.type = HTTPD_WS_TYPE_TEXT;
                
                // 将数据帧沿着建立好的 WebSocket 通道直接“推(push)”回去
                httpd_ws_send_frame(req, &ws_resp);
                
                free(json_response);
            }
        }
        free(buf);
    }
    return ret;
}


//处理chart.js请求
static esp_err_t chart_handler(httpd_req_t *req)
{
    // 设置响应类型为application/javascript
    httpd_resp_set_type(req, "application/javascript");
    // 设置内容编码为gzip
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    // 发送chart.js内容
    return httpd_resp_send(req, (const char *)_binary_chart_js_gz_start, _binary_chart_js_gz_end - _binary_chart_js_gz_start);
}

//处理时间同步请求
static esp_err_t time_sync_handler(httpd_req_t *req)
{
    // 如果系统已经通过 NTP 获得了准确时间，就不再接受网页端的同步
    if (g_is_ntp_synced) {
        ESP_LOGI(TAG, "拒网页同步: 系统已连接网络并启用高质量 NTP 时间");
        httpd_resp_sendstr(req, "OK"); // 虽然拒绝，但也给网页回OK让它别报错
        return ESP_OK;
    }

    char time_str[32];// 存储接收到的数据
    int ret;//记录读取到的字节数，判断读取是否成功
    int remaining = req->content_len;//记录剩余未读取的字节数

    if (remaining >= sizeof(time_str))
    {
        remaining = sizeof(time_str) - 1;//确保不会溢出
    }

    //读取前端发来的数据
    ret = httpd_req_recv(req, time_str, remaining);
    if (ret <= 0)    
    {
        // 读取失败，发送错误响应
        return ESP_FAIL;
    }
    time_str[ret] = '\0';//添加字符串结束符

    //解析时间字符串
    long timestamp = atol(time_str); //将字符串转换为长整数，得到时间戳
    if (timestamp >0)
    {
        //设置系统时间
        struct timeval tv = {
            .tv_sec = (time_t)timestamp, //将时间戳赋值给tv_sec
            .tv_usec = 0
        };
        settimeofday(&tv, NULL);//调用settimeofday函数设置系统时间

        //设置时区
        setenv("TZ", "CST-8", 1); //设置时区为中国标准时间
        tzset(); //应用时区设置

        time_sync_done = true;//设置时间同步完成的标志位

        //打印同步后的时间
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "系统时间同步为: %s", strftime_buf);

        //发送成功响应
        const char* response = "时间同步成功";
        return httpd_resp_send(req, response, strlen(response));
    }
    else
    {
        //时间戳无效，发送错误响应
        const char* response = "时间戳无效";
        return httpd_resp_send(req, response, strlen(response));
    }  
}

// 处理 Wi-Fi 配置请求的处理器 
static esp_err_t wifi_config_handler(httpd_req_t *req)
{
    char buffer[200]; // 用于存放接收的 JSON 字符串
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buffer)) {
        ESP_LOGE(TAG, "JSON 数据太大");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // 接收 HTTP 请求体数据
    ret = httpd_req_recv(req, buffer, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buffer[ret] = '\0'; // 结束符

    ESP_LOGI(TAG, "收到配网数据: %s", buffer);

    // 解析 JSON
    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON 解析失败");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    cJSON *pwd_item = cJSON_GetObjectItem(root, "password");

    if (ssid_item && pwd_item && cJSON_IsString(ssid_item) && cJSON_IsString(pwd_item)) {
        // 成功提取 SSID 和 密码
        const char *new_ssid = ssid_item->valuestring;
        const char *new_pwd = pwd_item->valuestring;
        ESP_LOGI(TAG, "准备连接 -> SSID: %s, Password: %s", new_ssid, new_pwd);
        // 保存到 NVS 
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
        // 应用新的 STA 配置
        wifi_config_t sta_config = {0};
        strncpy((char *)sta_config.sta.ssid, new_ssid, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char *)sta_config.sta.password, new_pwd, sizeof(sta_config.sta.password) - 1);
        
        // 断开现有的连接 -> 重新设置参数 -> 重新连接
        esp_wifi_disconnect();
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        esp_wifi_connect();

        // 等待获取 IP 地址 (最多等 8 秒)
        esp_netif_t *netif_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info;
        int retry_count = 0;
        bool got_ip = false;
        
        while (retry_count < 80) { // 80 * 100ms = 8s
            vTaskDelay(100 / portTICK_PERIOD_MS);
            if (netif_sta && esp_netif_get_ip_info(netif_sta, &ip_info) == ESP_OK) {
                if (ip_info.ip.addr != 0) { // IP 不为 0 说明拿到了
                    got_ip = true;
                    break;
                }
            }
            retry_count++;
        }

        // 发送带 IP 的响应
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

    // 释放 JSON 对象内存
    cJSON_Delete(root);

    return ESP_OK;
}

// 异步保存任务，防止大块擦除闪存时卡住整个网络
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

// 处理设置报警阈值的POST请求
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

        // 创建异步 NVS 存储任务，立即释放当前 HTTP 线程
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

// 定义一个函数，用于启动web服务器
httpd_handle_t start_webserver(void)
{
    // 从 NVS 中加载之前保存的报警阈值，如存在
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

    // 定义一个httpd_config_t类型的变量，用于存储httpd的配置信息
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // 允许服务器抛弃旧的闲置会话（Zombie Connection / 幽灵连接）
    // 防止手机App切换网络时没有发fin断开TCP，导致占满 socket 使其他端（比如PC）无法连接
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 10; // 给 WebSockets 足够的心跳容忍时间（前端每2秒发一次）

    // 定义一个httpd_handle_t类型的变量，用于存储httpd的句柄
    httpd_handle_t server = NULL;

    // 如果httpd_start函数返回值为ESP_OK，则表示启动成功
    if (httpd_start(&server, &config) == ESP_OK) {
        // 定义一个httpd_uri_t类型的变量，用于存储uri的配置信息
        httpd_uri_t index_uri = {
            // 设置uri的路径
            .uri       = "/",
            // 设置uri的方法为GET
            .method    = HTTP_GET,
            .handler   = index_handler,
            // 设置uri的用户上下文为NULL
            .user_ctx  = NULL
        };
        // 注册uri的处理函数
        httpd_register_uri_handler(server, &index_uri);


        // 定义数据API的URI
        httpd_uri_t data_uri = {
            .uri       = "/data",
            .method    = HTTP_GET,
            .handler   = data_handler,
            .user_ctx  = NULL
        };
        // 注册数据处理函数
        httpd_register_uri_handler(server, &data_uri);

        // 定义chart.js的URI
        httpd_uri_t chart_uri = {
            .uri       = "/chart.js",
            .method    = HTTP_GET,
            .handler   = chart_handler,
            .user_ctx  = NULL
        };
        // 注册chart.js处理函数
        httpd_register_uri_handler(server, &chart_uri);

        // 定义时间同步的URI
        httpd_uri_t time_sync_uri = {
            .uri       = "/sync_time",
            .method    = HTTP_POST, // POST方法
            .handler   = time_sync_handler,
            .user_ctx  = NULL
        };
        // 注册时间同步处理函数
        httpd_register_uri_handler(server, &time_sync_uri);

        // 注册设置报警值的处... (缩写，见下)
        httpd_uri_t set_alarm_uri = {
            .uri       = "/set_alarm",
            .method    = HTTP_POST,
            .handler   = set_alarm_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &set_alarm_uri);

        // 注册强大的 WebSocket 通信接口
        httpd_uri_t ws_uri = {
            .uri        = "/ws",
            .method     = HTTP_GET, // WebSocket 握手总是用 GET
            .handler    = ws_handler,
            .user_ctx   = NULL,
            .is_websocket = true    // 最核心代码：告诉系统这是专门处理 WebSocket 的接口
        };
        httpd_register_uri_handler(server, &ws_uri);

        // 注册配网接口的 URI
        httpd_uri_t wifi_config_uri = {
            .uri       = "/wifi_config",
            .method    = HTTP_POST,   // 前端用 POST 提交
            .handler   = wifi_config_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &wifi_config_uri);
    }

    // 返回httpd的句柄
    return server;
}