#include <string.h>
#include <esp_http_server.h>
#include "dht11.h"
// 嵌入资源（命名由 objcopy 自动生成）
extern const uint8_t _binary_index_html_start[];
extern const uint8_t _binary_index_html_end[];

// 嵌入 PNG 图片
extern const uint8_t _binary_back_png_start[];
extern const uint8_t _binary_back_png_end[];

// 处理index.html文件请求
static esp_err_t index_handler(httpd_req_t *req)
{
    // 设置响应类型为text/html
    httpd_resp_set_type(req, "text/html");

    // 获取当前温湿度数据
    int temp_int = get_temperature_int();
    int temp_dec = get_temperature_dec();
    int hum_int = get_humidity_int();
    int hum_dec = get_humidity_dec();

    // 生成动态 HTML
    char html_response[2048];
    snprintf(html_response, sizeof(html_response),
             "<!DOCTYPE html>"
             "<html>"
             "<head>"
             "<meta charset=\"UTF-8\" />"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />"
             "<title>ESP32 Web Server</title>"
             "<style>"
             "body {"
             "background-color: #202020;"
             "color: #fff;"
             "text-align: center;"
             "height: 100vh;"
             "margin: 0;"
             "display: flex;"
             "flex-direction: column;"
             "justify-content: center;"
             "align-items: center;"
             "font-family: Arial, sans-serif;"
             "background-image: url('/back.png');"
             "background-size: cover;"
             "background-repeat: no-repeat;"
             "background-position: center;"
             "}"
             "h1 { font-size: 2em; margin-bottom: 20px; }"
             ".data { font-size: 1.5em; margin: 10px 0; }"
             "</style>"
             "<script>"
             "async function updateData() {"
             "    try {"
             "        const response = await fetch('/data');"
             "        const data = await response.json();"
             "        document.getElementById('temp').textContent = data.temperature + ' °C';"
             "        document.getElementById('hum').textContent = data.humidity + ' %%';"
             "    } catch (error) {"
             "        console.error('Error fetching data:', error);"
             "    }"
             "}"
             "setInterval(updateData, 3000); // 每3秒更新一次"
             "</script>"
             "</head>"
             "<body>"
             "<h1>ESP32 Sensor Data</h1>"
             "<div class=\"data\">Temperature: <span id=\"temp\">%d.%d °C</span></div>"
             "<div class=\"data\">Humidity: <span id=\"hum\">%d.%d %%</span></div>"
             "</body>"
             "</html>",
             temp_int, temp_dec, hum_int, hum_dec);

    // 发送动态 HTML
    return httpd_resp_send(req, html_response, strlen(html_response));
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

    // 生成 JSON
    char json_response[128];
    snprintf(json_response, sizeof(json_response),
             "{\"temperature\": \"%d.%d\", \"humidity\": \"%d.%d\"}",
             temp_int, temp_dec, hum_int, hum_dec);

    // 发送 JSON
    return httpd_resp_send(req, json_response, strlen(json_response));
}

// 处理back.png图片请求
static esp_err_t png_handler(httpd_req_t *req)
{
    // 设置响应类型为image/png
    httpd_resp_set_type(req, "image/png");
    // 发送PNG图片内容
    return httpd_resp_send(req, (const char *)_binary_back_png_start, _binary_back_png_end - _binary_back_png_start);
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

        // 定义PNG图片的URI
        httpd_uri_t png_uri = {
            .uri       = "/back.png",
            .method    = 1,
            .handler   = png_handler,
            .user_ctx  = NULL
        };
        // 注册PNG处理函数
        httpd_register_uri_handler(server, &png_uri);

        // 定义数据API的URI
        httpd_uri_t data_uri = {
            .uri       = "/data",
            .method    = 1,
            .handler   = data_handler,
            .user_ctx  = NULL
        };
        // 注册数据处理函数
        httpd_register_uri_handler(server, &data_uri);
    }

    // 返回httpd的句柄
    return server;
}