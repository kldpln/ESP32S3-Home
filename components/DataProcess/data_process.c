#include <string.h>
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "data_process.h"
#include "dht11_rmt.h"

#define DHT11_GPIO 7
const static char *TAG = "DHT11";

/* 最近 7 天的历史数据，索引 0 表示昨天。 */
static DailyData history_data[7];

/* 当日极值统计。 */
static float curr_max_temp;
static float curr_min_temp;
static float curr_max_hum;
static float curr_min_hum;

/* 最近一次完成日结算的日期。 */
static int last_processed_weekday = -1;
static bool first_read = true;
static const char* NVS_NAMESPACE = "history";

/* 当前采样缓存。 */
static uint8_t buffer[5];
static float current_temperature = 0.0f;
static float current_humidity = 0.0f;
static bool current_reading_valid = false;

/* 初始化 DHT11 采样模块。 */
void data_process_init()
{
    dht11_rmt_init((gpio_num_t)DHT11_GPIO);

    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(history_data);
        err = nvs_get_blob(my_handle, "history", history_data, &required_size);
        if (err != ESP_OK) {
            memset(history_data, 0, sizeof(history_data));
        }

        nvs_get_i32(my_handle, "last_weekday", (int32_t*)&last_processed_weekday);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "历史数据从NVS加载成功");
    } else {
        ESP_LOGW(TAG, "Error (%s) opening NVS handle", esp_err_to_name(err));
    }

    vTaskDelay(1200 / portTICK_PERIOD_MS);
}

/* 保存历史统计数据到 NVS。 */
static void save_history_to_nvs() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_blob(my_handle, "history", history_data, sizeof(history_data));
        nvs_set_i32(my_handle, "last_weekday", last_processed_weekday);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "数据已保存到 NVS");
    } else {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle for writing", esp_err_to_name(err));
    }
}

/* DHT11 采样任务。 */
static void data_process_task(void *pvParameters)
{
    while (1)
    {
        dht11_reading_t rmt_data;
        esp_err_t result = dht11_rmt_read(&rmt_data);
        
        if (result == ESP_OK)
        {
            /* 上一次有效读数，用于异常筛选。 */
            static float last_valid_temp = -999.0;
            static float last_valid_hum = -999.0;
            bool valid = true;

            /* 读取当前温湿度值。 */
            float temp = rmt_data.temperature;
            float hum = rmt_data.humidity;

            /* 异常值过滤。 */
            if(last_valid_temp != -999.0){
                if (abs(temp - last_valid_temp) >10.0 || abs(hum - last_valid_hum) > 30.0)
                {
                    ESP_LOGW(TAG, "突发数据异常：温度 %.1f, 湿度 %.1f，已过滤", temp, hum);
                    valid = false;
                }
            }

            if(valid){
                last_valid_temp = temp;
                last_valid_hum = hum;
                current_temperature = temp;
                current_humidity = hum;
                current_reading_valid = true;
            } else {
                current_reading_valid = false;
                ESP_LOGW(TAG, "当前采样无效，等待下一次有效数据");
            }

            /* 仅在有效采样时更新显示缓存。 */
            if (current_reading_valid) {
                buffer[2] = (int)temp;
                buffer[3] = (int)((temp - buffer[2]) * 10);
                buffer[0] = (int)hum;
                buffer[1] = (int)((hum - buffer[0]) * 10);
            }

            /* 更新当日极值。 */
            if (first_read) {
                curr_max_temp = temp;
                curr_min_temp = temp;
                curr_max_hum = hum;
                curr_min_hum = hum;
                first_read = false;
            } else {
                if (temp > curr_max_temp) curr_max_temp = temp;
                if (temp < curr_min_temp) curr_min_temp = temp;
                if (hum > curr_max_hum) curr_max_hum = hum;
                if (hum < curr_min_hum) curr_min_hum = hum;
            }

            /* 检查时间同步状态。 */
            time_t now = time(NULL);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);

            /* 仅在时间同步后执行日结算。 */
            static bool time_synced_once = false;
            if (timeinfo.tm_year > (2020 - 1900)) {
                if (!time_synced_once){
                    time_synced_once = true;
                    last_processed_weekday = timeinfo.tm_mday;
                    ESP_LOGI("Time", "时间同步恢复，重置日期锚点，暂不结算历史数据");
                    first_read = true;
                }

                else{
                int today = timeinfo.tm_mday;
                    if (today != last_processed_weekday && last_processed_weekday != -1) {
                    ESP_LOGI("Time", "检测到跨天，从%d变为%d", last_processed_weekday, today);

                    /* 历史数据整体后移。 */
                    for (int i = 6; i > 0; i--) {
                        history_data[i] = history_data[i-1];
                    }

                    /* 归档昨日统计结果。 */
                    history_data[0].max_temp = curr_max_temp;
                    history_data[0].min_temp = curr_min_temp;
                    history_data[0].max_hum  = curr_max_hum;
                    history_data[0].min_hum  = curr_min_hum;
                    history_data[0].timestamp = now - 86400;
                    history_data[0].weekday = (timeinfo.tm_wday - 1 + 7) % 7;
                    history_data[0].valid = true;

                    /* 持久化历史数据。 */
                    last_processed_weekday = today;
                    save_history_to_nvs();
                    first_read = true;

                ESP_LOGI("Time", "24h周期重置 - 昨天的统计数据已保存到NVS");
                    
                    }
                }
            }
        }
        else
        {
            ESP_LOGE(TAG, "Reading data failed.");
            current_reading_valid = false;
        }
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

/* 启动 DHT11 读取任务。 */
void data_process_start_task(void)
{
    xTaskCreatePinnedToCore(data_process_task, "data_process_task", 4096, NULL, 5, NULL, 1);
}

/* 获取温度整数部分。 */
int get_temperature_int(void)
{
    return buffer[2];
}

/* 获取温度小数部分。 */
int get_temperature_dec(void)
{
    return buffer[3];
}

/* 获取湿度整数部分。 */
int get_humidity_int(void)
{
    return buffer[0];
}

/* 获取湿度小数部分。 */
int get_humidity_dec(void)
{
    return buffer[1];
}

bool get_current_reading_valid(void)
{
    return current_reading_valid;
}

float get_current_temperature(void)
{
    return current_temperature;
}

float get_current_humidity(void)
{
    return current_humidity;
}

/* 获取当日温湿度极值。 */
void get_today_stats(float *max_t, float *min_t, float *max_h, float *min_h)
{
    *max_t = curr_max_temp;
    *min_t = curr_min_temp;
    *max_h = curr_max_hum;
    *min_h = curr_min_hum;
}

/* 获取最近 7 天历史数据。 */
void get_weekly_history(DailyData *history_array)
{
    if (history_array != NULL) {
        memcpy(history_array, history_data, sizeof(history_data));
    }
}