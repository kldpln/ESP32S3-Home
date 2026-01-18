#include <string.h>
#include <esp_http_server.h>
#include "dht11.h"
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

    // 获取当前温湿度数据
    int temp_int = get_temperature_int();
    int temp_dec = get_temperature_dec();
    int hum_int = get_humidity_int();
    int hum_dec = get_humidity_dec();

    float max_t = get_max_temp();
    float min_t = get_min_temp();
    float max_h = get_max_hum();
    float min_h = get_min_hum();

    // 生成 JSON
    char json_response[256];
    snprintf(json_response, sizeof(json_response),
             "{\"temperature\": \"%d.%d\", \"humidity\": \"%d.%d\", \"max_temp\": \"%.1f\", \"min_temp\": \"%.1f\", \"max_hum\": \"%.1f\", \"min_hum\": \"%.1f\"}",
             temp_int, temp_dec, hum_int, hum_dec, max_t, min_t, max_h, min_h);

    // 发送 JSON
    return httpd_resp_send(req, json_response, strlen(json_response));
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
            .method    = 1,
            .handler   = index_handler,
            // 设置uri的用户上下文为NULL
            .user_ctx  = NULL
        };
        // 注册uri的处理函数
        httpd_register_uri_handler(server, &index_uri);

        // 定义JPG图片的URI
        httpd_uri_t jpg_uri = {
            .uri       = "/back.jpg",
            .method    = 1,
            .handler   = jpg_handler,
            .user_ctx  = NULL
        };
        // 注册JPG处理函数
        httpd_register_uri_handler(server, &jpg_uri);

        // 定义数据API的URI
        httpd_uri_t data_uri = {
            .uri       = "/data",
            .method    = 1,
            .handler   = data_handler,
            .user_ctx  = NULL
        };
        // 注册数据处理函数
        httpd_register_uri_handler(server, &data_uri);

        // 定义chart.js的URI
        httpd_uri_t chart_uri = {
            .uri       = "/chart.js",
            .method    = 1,
            .handler   = chart_handler,
            .user_ctx  = NULL
        };
        // 注册chart.js处理函数
        httpd_register_uri_handler(server, &chart_uri);
    }

    // 返回httpd的句柄
    return server;
}