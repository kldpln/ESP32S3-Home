#include "dht11_rmt.h"
#include "driver/rmt_rx.h"  // RMT 接收通道的头文件
#include "esp_log.h"
#include "rom/ets_sys.h"  // 用于 ets_delay_us() 函数
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "DHT11_RMT";

// RMT 接收通道句柄
static rmt_channel_handle_t rx_channel = NULL; 
static gpio_num_t dht11_gpio  = GPIO_NUM_NC;
// 数据接收队列
static QueueHandle_t rx_receive_queue = NULL;

// 接收完成回调函数
static bool IRAM_ATTR example_rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

esp_err_t dht11_rmt_init(gpio_num_t gpio_num)
{
    if (rx_channel != NULL) {
        ESP_LOGW(TAG, "RMT 接收通道已初始化");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "初始化RMT接收通道为 GPIO %d", gpio_num);

    rmt_rx_channel_config_t rx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,       // 1MHz, 1us = 1 tick
        .mem_block_symbols = 64,        // RMT符号内存块大小, 64 sufficient for DHT11
        .gpio_num = gpio_num,
    };

    esp_err_t err = rmt_new_rx_channel(&rx_chan_config, &rx_channel);
    if (err != ESP_OK) return err;

    dht11_gpio = gpio_num;
    
    // 新建接收数据队列
    rx_receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    if (!rx_receive_queue) {
        ESP_LOGE(TAG, "创建队列失败");
        return ESP_FAIL;
    }

    // 注册接收完成回调函数
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = example_rmt_rx_done_callback,
    };
    err = rmt_rx_register_event_callbacks(rx_channel, &cbs, rx_receive_queue);
    if (err != ESP_OK) return err;

    err = rmt_enable(rx_channel);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

esp_err_t dht11_rmt_read(dht11_reading_t *data)
{
    if (data == NULL || rx_channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 清空上次可能遗留的队列数据
    xQueueReset(rx_receive_queue);

    // 发送 20ms 开始信号脉冲启动 DHT11 单总线
    gpio_set_direction(dht11_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(dht11_gpio, 1);
    ets_delay_us(1000);
    
    gpio_set_level(dht11_gpio, 0);
    ets_delay_us(20000);
    
    //  拉高 20us
    gpio_set_level(dht11_gpio, 1);
    ets_delay_us(20);
    
    // 信号线设置为输入，并开启上拉，准备接收数据
    gpio_set_direction(dht11_gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(dht11_gpio, GPIO_PULLUP_ONLY);

    // 配置并启动 RMT 接收
    rmt_receive_config_t receive_config = { 
        .signal_range_min_ns = 100,             // 最小 0.1us，视为干扰
        .signal_range_max_ns = 1000 * 1000,     // 最大 1000us (1ms)，超过判断为结束
    };

    static rmt_symbol_word_t raw_symbols[128];      
    esp_err_t err = rmt_receive(rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT 启动接收失败: %s", esp_err_to_name(err));
        return err;
    }

    // 等待回调通知完成 (最多等 1000ms)
    rmt_rx_done_event_data_t rx_data;
    if (xQueueReceive(rx_receive_queue, &rx_data, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "接收超时");
        rmt_disable(rx_channel);
        rmt_enable(rx_channel);
        return ESP_ERR_TIMEOUT;
    }

    // 使用精准线性波形解析法（消除起始段杂波错位导致的左移翻倍问题）
    int durations[160] = {0};
    int levels[160] = {0};
    int pulse_count = 0;

    // 将 RMT 中混乱的符号组展开成一维的单纯电平和时长数组
    for (int i = 0; i < rx_data.num_symbols && pulse_count < 158; i++) {
        if (rx_data.received_symbols[i].duration0 > 0) {
            durations[pulse_count] = rx_data.received_symbols[i].duration0;
            levels[pulse_count] = rx_data.received_symbols[i].level0;
            pulse_count++;
        }
        if (rx_data.received_symbols[i].duration1 > 0) {
            durations[pulse_count] = rx_data.received_symbols[i].duration1;
            levels[pulse_count] = rx_data.received_symbols[i].level1;
            pulse_count++;
        }
    }

    uint8_t dht11_bytes[5] = {0};
    int bit_index = 0;

    // 智能锁定，不盲目依赖固定索引：
    for (int i = 1; i < pulse_count; i++) {
        // 找寻一次标准的数据位开始：高电平之前的低电平应该在 50us 左右
        if (levels[i] == 1 && levels[i-1] == 0) {
            if (durations[i-1] >= 30 && durations[i-1] <= 75) {
                // 判断此高电平时长：标准中 26-28us 代表0，70us 代表 1。以 40us 为楚河汉界。
                bool is_bit_one = (durations[i] > 40);

                dht11_bytes[bit_index / 8] <<= 1;
                if (is_bit_one) {
                    dht11_bytes[bit_index / 8] |= 1;
                }

                bit_index++;
                if (bit_index >= 40) {
                    break;
                }
            }
        }
    }

    if (bit_index < 40) {
        ESP_LOGE(TAG, "数据解析不完整，仅获取 %d bits (左移错误的根源)", bit_index);
        return ESP_ERR_INVALID_SIZE;
    }

    // 校验数据
    uint8_t checksum = dht11_bytes[0] + dht11_bytes[1] + dht11_bytes[2] + dht11_bytes[3];
    if (checksum != dht11_bytes[4]) {
        ESP_LOGE(TAG, "Checksum failure: calc:%02X != recv:%02X", checksum, dht11_bytes[4]);
        return ESP_FAIL;
    }

    // 转换为 Float 返回。
    // DHT11数据结构: Byte0=湿度整数, Byte1=湿度小数, Byte2=温度整数, Byte3=温度小数
    data->humidity = dht11_bytes[0] + dht11_bytes[1] * 0.1f;
    data->temperature = dht11_bytes[2] + (dht11_bytes[3] & 0x7F) * 0.1f;
    
    // 处理负温 (如果温度第四字节的最高位是1)
    if (dht11_bytes[3] & 0x80) {
        data->temperature = -data->temperature;
    }

    ESP_LOGI(TAG, "Read Success! Temp: %.1f, Hum: %.1f", data->temperature, data->humidity);
    return ESP_OK;
}