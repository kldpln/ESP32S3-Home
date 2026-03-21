#ifndef _DHT11_RMT_H_
#define _DHT11_RMT_H_

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"


// 1. 定义一个结构体来保存温湿度数据
typedef struct {
    float temperature;  // 温度，包含小数部分
    float humidity;     // 湿度，包含小数部分
} dht11_reading_t;

// 2. 初始化函数声明
// 只需要告诉它 DHT11 接在哪根 GPIO 上即可
esp_err_t dht11_rmt_init(gpio_num_t gpio_num);

// 3. 读取数据的函数声明
// 传入一个 dht11_reading_t 的指针，函数会把读到的数据塞进这个结构体里
esp_err_t dht11_rmt_read(dht11_reading_t *data);


#endif // _DHT11_RMT_H_