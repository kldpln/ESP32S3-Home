#include <string.h>
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "dht11.h"

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
static uint8_t prebuffer[5];
static int64_t phase_duration[3] = {0};
static int64_t bit_duration_low[40] = {0};
static int64_t bit_duration_high[40] = {0};

// DHT11 初始化引脚，等待1s上电时间
void dht11_init()
{
    gpio_config_t cnf = {
        .mode = GPIO_MODE_OUTPUT_OD,
        .pin_bit_mask = 1ULL << DHT11_GPIO,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&cnf);
    gpio_set_level(DHT11_GPIO, 1);
    
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

/*
timeout单位是us
*/
static esp_err_t wait_pin_state(uint32_t timeout, int expected_pin_state)
{
    int64_t start_time;
    start_time = esp_timer_get_time();
    while (esp_timer_get_time() - start_time <= timeout)
    {
        if (gpio_get_level(DHT11_GPIO) == expected_pin_state)
            return ESP_OK;
        esp_rom_delay_us(1);
    }
    return ESP_FAIL;
}

// 读取数据函数
static esp_err_t DataRead()
{
    int64_t time_since_waiting_start;
    esp_err_t result = ESP_FAIL;
    memset(prebuffer, 0, sizeof(prebuffer));
    // 发送开始信号
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_OUTPUT_OD);
    // 起始前先确保总线为高
    gpio_set_level(DHT11_GPIO, 1);
    esp_rom_delay_us(10);         // 小等一下，10us

    // 起始信号：拉低至少 18ms，这里给 20ms
    gpio_set_level(DHT11_GPIO, 0);
    vTaskDelay(20 / portTICK_PERIOD_MS);  // 20ms

    // 释放总线：拉高 20~40us
    gpio_set_level(DHT11_GPIO, 1);
    esp_rom_delay_us(30);         // 30us

    // 转为输入，交给从机
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_INPUT);

    time_since_waiting_start = esp_timer_get_time();

    result=wait_pin_state(120,0);  //
    if(result == ESP_FAIL)
    {
        ESP_LOGE(TAG, "Phase A Fail, slave not set LOW.");
        return ESP_FAIL;
    }
    phase_duration[0]=esp_timer_get_time()-time_since_waiting_start;
    time_since_waiting_start=esp_timer_get_time();
    /*等从机拉低总线83us，再拉高87us*/
    result=wait_pin_state(120,1);
        if (result == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Phase B Fail, slave not set HIGH.");
            return ESP_FAIL;
        }
    phase_duration[1]=esp_timer_get_time()-time_since_waiting_start;
    time_since_waiting_start=esp_timer_get_time();
    result = wait_pin_state(120, 0);
        if (result == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Phase C Fail, slave not set LOW to start sending.");
            return ESP_FAIL;
        }
    phase_duration[2]=esp_timer_get_time()-time_since_waiting_start;
    time_since_waiting_start=esp_timer_get_time();

    // 读取40位数据
    // 进入临界区，禁止中断，确保读取过程中不被打断，临界区内若使用return语句，请确保在返回前调用portEXIT_CRITICAL(&mux)退出临界区
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;  
    portENTER_CRITICAL(&mux);

    for (int j = 0; j < 5; j++) {
        for (int i = 0; i < 8; i++) {

        // 等低电平结束（bit 前导低电平），加超时保护
        int64_t t_start = esp_timer_get_time();
        while (gpio_get_level(DHT11_GPIO) == 0) {
            if (esp_timer_get_time() - t_start > 100) {
                portEXIT_CRITICAL(&mux); // 退出临界区
                ESP_LOGE(TAG, "Bit %d low too long", j*8 + i);
                return ESP_FAIL;
            }
            esp_rom_delay_us(1);
        }
        bit_duration_low[j*8+i] = esp_timer_get_time() - time_since_waiting_start;

        // 测高电平持续时间
        time_since_waiting_start = esp_timer_get_time();
        while (gpio_get_level(DHT11_GPIO) == 1) {
            if (esp_timer_get_time() - time_since_waiting_start > 100) {
                break; // 超过 100us 就退出
            }
            esp_rom_delay_us(1);
        }

        int64_t high_time = esp_timer_get_time() - time_since_waiting_start;
        bit_duration_high[j*8+i] = high_time;

        if (high_time > 40) {
            prebuffer[j] |= (1U << (7 - i)); // 1
        }
    }
}

    portEXIT_CRITICAL(&mux); // 退出临界区，允许中断
 
    result=wait_pin_state(56,1);
    if (result == ESP_FAIL)
    {
        ESP_LOGE(TAG, "Data is all read.But CAN not set high.");
        return ESP_FAIL;
    }

    // 量程外值检查
    float hum = prebuffer[0] + prebuffer[1] / 10.0f;
    float temp = prebuffer[2] + prebuffer[3] / 10.0f;

    // 量程范围: Temp -20~60, Hum 5~95
    if ((temp >= -20 && temp <= 60) && (hum >= 5 && hum <= 90)) {
        memcpy(buffer, prebuffer, sizeof(buffer));
    } else {
        ESP_LOGE(TAG, "Data out of range! Temp: %.1f, Hum: %.1f", temp, hum);
        return ESP_FAIL;
    }

    return ESP_OK;
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
static void dht11_task(void *pvParameters)
{


    while (1)
    {
        memset(phase_duration, 0, sizeof(phase_duration));
        memset(bit_duration_low, 0, sizeof(bit_duration_low));
        memset(bit_duration_high, 0, sizeof(bit_duration_high));
        esp_err_t result = DataRead();
        if (result == ESP_OK)
        {

            ESP_LOGI(TAG, "温度:%d.%d, 湿度:%d.%d", buffer[2], buffer[3], buffer[0], buffer[1]);

            // 最大最小值检测
            float temp = buffer[2] + buffer[3] / 10.0f;
            float hum = buffer[0] + buffer[1] / 10.0f;

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
            if (timeinfo.tm_year > (2020 - 1900)) {
                int today = timeinfo.tm_mday;

                // 刚开机还没记录过日期
                if (last_processed_weekday == -1) {
                    last_processed_weekday = today;
                }
                // 跨天了！(比如从10号变11号)
                else if (today != last_processed_weekday) {
                    ESP_LOGI(TAG, "检测到跨天，从%d变为%d", last_processed_weekday, today);

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

                ESP_LOGI(TAG, "24h周期重置 - 昨天的统计数据已保存到NVS");
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
void dht11_start_task(void)
{
    // 固定到核心 1，高优先级 5
    xTaskCreatePinnedToCore(dht11_task, "dht11_task", 4096, NULL, 5, NULL, 1);
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