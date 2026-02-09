#include <string.h>
#include <esp_http_server.h>
#include "dht11.h"
#include "sys/time.h"
#include "time.h"
#include "esp_log.h"


//时间设置标志位
bool time_sync_done = false;

// 嵌入资源（命名由 objcopy 自动生成）
extern const uint8_t _binary_index_html_start[];
extern const uint8_t _binary_index_html_end[];

// 嵌入 JPG 图片
extern const uint8_t _binary_back_jpg_start[];
extern const uint8_t _binary_back_jpg_end[];

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

// 处理数据请求，返回 JSON
static esp_err_t data_handler(httpd_req_t *req)
{
    // 设置响应类型为application/json
    httpd_resp_set_type(req, "application/json");

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
        return ESP_FAIL;
    }
    //今日数据
    int offset = 0;
    offset += sprintf(json_response + offset, 
             "{\"temperature\": \"%d.%d\", \"humidity\": \"%d.%d\", "
             "\"max_temp_today\": \"%.1f\", \"min_temp_today\": \"%.1f\", "
             "\"max_hum_today\": \"%.1f\", \"min_hum_today\": \"%.1f\", "
             "\"history\": [", 
             temp_int, temp_dec, hum_int, hum_dec, 
             max_t_today, min_t_today, max_h_today, min_h_today);

    // 循环写入历史数组
    for (int i = 0; i < 7; i++) {
        // 如果数据无效，就填 null 或者 0，前端判断 valid 字段
        if (history[i].valid) {
             offset += sprintf(json_response + offset, 
                "{\"day_ago\": %d, \"weekday\": %d, \"max_temp\": %.1f, \"min_temp\": %.1f, \"max_hum\": %.1f},", 
                i + 1, history[i].weekday, history[i].max_temp, history[i].min_temp, history[i].max_hum);
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

    // 发送
    httpd_resp_send(req, json_response, strlen(json_response));
    
    // 释放内存
    free(json_response);
    
    return ESP_OK;   
}

// 处理back.jpg图片请求
static esp_err_t jpg_handler(httpd_req_t *req)
{
    // 设置响应类型为image/jpeg
    httpd_resp_set_type(req, "image/jpeg");
    // 发送JPG图片内容
    return httpd_resp_send(req, (const char *)_binary_back_jpg_start, _binary_back_jpg_end - _binary_back_jpg_start);
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
        ESP_LOGI("Time Sync", "系统时间同步为: %s", strftime_buf);

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
// 定义一个函数，用于启动web服务器
httpd_handle_t start_webserver(void)
{
    // 定义一个httpd_config_t类型的变量，用于存储httpd的配置信息
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

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

        // 定义JPG图片的URI
        httpd_uri_t jpg_uri = {
            .uri       = "/back.jpg",
            .method    = HTTP_GET,
            .handler   = jpg_handler,
            .user_ctx  = NULL
        };
        // 注册JPG处理函数
        httpd_register_uri_handler(server, &jpg_uri);

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
    }

    // 返回httpd的句柄
    return server;
}