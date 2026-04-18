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
#include "dht11_rmt.h" // 引入 RMT 驱动

#define DHT11_GPIO 7  // DHT11引脚定义
const static char *TAG = "DHT11";

//历史数据
static DailyData history_data[7]; // 存储最近7天的数据,0表示昨天，1表示前天,,,

//今日极值
static float curr_max_temp;
static float curr_min_temp;
static float curr_max_hum;
static float curr_min_hum;

static int last_processed_weekday = -1; // 上次处理数据的星期几，初始值为-1表示未处理过
static bool first_read = true;
static const char* NVS_NAMESPACE = "history";

// 实时温度 湿度buffer
static uint8_t buffer[5];

// DHT11 初始化引脚，等待1s上电时间
void data_process_init()
{
    // 初始化 RMT 底层驱动代替原有的 GPIO 手动配置
    dht11_rmt_init((gpio_num_t)DHT11_GPIO);
    
    // 从NVS中读取数据
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(history_data);
        err = nvs_get_blob(my_handle, "history", history_data, &required_size);
        if (err != ESP_OK)  memset(history_data, 0, sizeof(history_data)); // 如果读取失败，初始化为0
        
        //读取上次处理的日期
        nvs_get_i32(my_handle, "last_weekday", (int32_t*)&last_processed_weekday);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "历史数据从NVS加载成功");
    } else {
        ESP_LOGW(TAG, "Error (%s) opening NVS handle", esp_err_to_name(err));
    }

    vTaskDelay(1200 / portTICK_PERIOD_MS);
}

// 保存统计数据到 NVS
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

// DHT11 任务函数
static void data_process_task(void *pvParameters)
{
    while (1)
    {
        dht11_reading_t rmt_data;
        esp_err_t result = dht11_rmt_read(&rmt_data);
        
        if (result == ESP_OK)
        {
            // 最大最小值检测

            // 定义上一次的有效读数，用于对比
            static float last_valid_temp = -999.0;
            static float last_valid_hum = -999.0;
            bool valid = true; //数据有效标签

            //温湿度计算 (直接使用 rmt 读到的数据，更加精确)
            float temp = rmt_data.temperature;
            float hum = rmt_data.humidity;

            //异常值过滤
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
            } else {
                //如果数据异常但又没有历史数据可用，就暂时接受这个异常值，避免数据完全中断
                if(last_valid_temp == -999.0){
                    last_valid_temp = temp;
                    last_valid_hum = hum;
                    ESP_LOGW(TAG, "没有历史数据可用，接受当前异常值作为初始值");
                } else {
                    temp = last_valid_temp;
                    hum = last_valid_hum;
                    ESP_LOGW(TAG, "使用上次有效数据：温度 %.1f, 湿度 %.1f", temp, hum);
                }
            }
            
            // 更新缓存（放在异常值处理之后，保证 Web 端拿到的也是清洗后的安全数据！）
            buffer[2] = (int)temp;                           // 温度整数
            buffer[3] = (int)((temp - buffer[2]) * 10);      // 温度小数
            buffer[0] = (int)hum;                            // 湿度整数
            buffer[1] = (int)((hum - buffer[0]) * 10);       // 湿度小数

            //最值对比
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

            //时间同步检测
            time_t now = time(NULL);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);

            // 只有时间同步过才处理
            static bool time_synced_once = false; // 首次同步标志
            if (timeinfo.tm_year > (2020 - 1900)) {
                if (!time_synced_once){
                    time_synced_once = true;
                    last_processed_weekday = timeinfo.tm_mday; // 初始化为当前日期，避免开机就误判跨天
                    ESP_LOGI("Time", "时间同步恢复，重置日期锚点，暂不结算历史数据");
                    first_read = true; // 时间同步后重置极值，避免历史数据被新一天的异常值污染
                }

                else{
                int today = timeinfo.tm_mday;
                // 跨天了！(比如从10号变11号)
                    if (today != last_processed_weekday && last_processed_weekday != -1) {
                    ESP_LOGI("Time", "检测到跨天，从%d变为%d", last_processed_weekday, today);

                    // 数组移位
                    for (int i = 6; i > 0; i--) {
                        history_data[i] = history_data[i-1];
                    }

                    // 结算昨天 (current stats 就是昨天一整跑下来的结果)
                    history_data[0].max_temp = curr_max_temp;
                    history_data[0].min_temp = curr_min_temp;
                    history_data[0].max_hum  = curr_max_hum;
                    history_data[0].min_hum  = curr_min_hum;
                    history_data[0].timestamp = now - 86400; // 昨天的时刻
                    history_data[0].weekday = (timeinfo.tm_wday - 1 + 7) % 7; // 昨天是周几
                    history_data[0].valid = true;

                    // 保存
                    last_processed_weekday = today;
                    save_history_to_nvs();
                    first_read = true; // 新的一天，重置极值

                ESP_LOGI("Time", "24h周期重置 - 昨天的统计数据已保存到NVS");
                    
                    }
                }
            }
        }
        else
        {
            ESP_LOGE(TAG, "Reading data failed.");
        }
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

// 启动 DHT11 读取任务
void data_process_start_task(void)
{
    // 固定到核心 1，高优先级 5
    xTaskCreatePinnedToCore(data_process_task, "data_process_task", 4096, NULL, 5, NULL, 1);
}

// 获取温度（整数部分）
int get_temperature_int(void)
{
    return buffer[2];
}

// 获取温度（小数部分）
int get_temperature_dec(void)
{
    return buffer[3];
}

// 获取湿度（整数部分）
int get_humidity_int(void)
{
    return buffer[0];
}

// 获取湿度（小数部分）
int get_humidity_dec(void)
{
    return buffer[1];
}

// 获取今日最大最小值
void get_today_stats(float *max_t, float *min_t, float *max_h, float *min_h)
{
    *max_t = curr_max_temp;
    *min_t = curr_min_temp;
    *max_h = curr_max_hum;
    *min_h = curr_min_hum;
}

// 获取昨日最大最小值
void get_weekly_history(DailyData *history_array)
{
    // 将内部存储的历史数据复制给调用者
    if (history_array != NULL) {
        memcpy(history_array, history_data, sizeof(history_data));
    }
}