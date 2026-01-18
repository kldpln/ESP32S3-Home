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

// Stats
static float saved_max_temp = -999.0;
static float saved_min_temp = 999.0;
static float saved_max_hum = -999.0;
static float saved_min_hum = 999.0;

static float curr_max_temp = -999.0;
static float curr_min_temp = 999.0;
static float curr_max_hum = -999.0;
static float curr_min_hum = 999.0;

static bool stats_saved = false;
static const char* NVS_NAMESPACE = "storage";

// 温度 湿度buffer
static uint8_t buffer[5];
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
    
    // Load stats from NVS
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        // NVS only supports integers well, but we can store blobs or scaled ints.
        // For simplicity, let's store blobs for floats.
        size_t required_size = sizeof(float);
        nvs_get_blob(my_handle, "max_temp", &saved_max_temp, &required_size);
        nvs_get_blob(my_handle, "min_temp", &saved_min_temp, &required_size);
        nvs_get_blob(my_handle, "max_hum", &saved_max_hum, &required_size);
        nvs_get_blob(my_handle, "min_hum", &saved_min_hum, &required_size);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Stats loaded from NVS");
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
    memset(buffer, 0, sizeof(buffer));

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
    for (int j = 0; j < 5; j++) {
        for (int i = 0; i < 8; i++) {

        // 等低电平结束（bit 前导低电平），加超时保护
        int64_t t_start = esp_timer_get_time();
        while (gpio_get_level(DHT11_GPIO) == 0) {
            if (esp_timer_get_time() - t_start > 100) {
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
            buffer[j] |= (1U << (7 - i)); // 1
        }
    }
}

 
    result=wait_pin_state(56,1);
    if (result == ESP_FAIL)
    {
        ESP_LOGE(TAG, "Data is all read.But CAN not set high.");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void save_stats_to_nvs() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_blob(my_handle, "max_temp", &saved_max_temp, sizeof(float));
        nvs_set_blob(my_handle, "min_temp", &saved_min_temp, sizeof(float));
        nvs_set_blob(my_handle, "max_hum", &saved_max_hum, sizeof(float));
        nvs_set_blob(my_handle, "min_hum", &saved_min_hum, sizeof(float));
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Stats saved to NVS");
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
            ESP_LOGI(TAG, "Reading data succeed.");
            ESP_LOGI(TAG, "Temperature is:%d.%d, Humidity is:%d.%d", buffer[2], buffer[3], buffer[0], buffer[1]);

            // Stats Update Logic
            float temp = buffer[2] + buffer[3] / 10.0f;
            float hum = buffer[0] + buffer[1] / 10.0f;

            if (temp > curr_max_temp) curr_max_temp = temp;
            if (temp < curr_min_temp) curr_min_temp = temp;
            if (hum > curr_max_hum) curr_max_hum = hum;
            if (hum < curr_min_hum) curr_min_hum = hum;

            // Check 5 minutes
            if (!stats_saved && (esp_timer_get_time() > 300000000)) { // 300 seconds * 1000000 us
                saved_max_temp = curr_max_temp;
                saved_min_temp = curr_min_temp;
                saved_max_hum = curr_max_hum;
                saved_min_hum = curr_min_hum;
                save_stats_to_nvs();
                stats_saved = true;
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

// Getters for Stats
float get_max_temp(void) { return saved_max_temp; }
float get_min_temp(void) { return saved_min_temp; }
float get_max_hum(void) { return saved_max_hum; }
float get_min_hum(void) { return saved_min_hum; }