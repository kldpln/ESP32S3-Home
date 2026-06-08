#ifndef _DHT11_RMT_H_
#define _DHT11_RMT_H_

#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"


/* 温湿度读取结果。 */
typedef struct {
    float temperature;  /* 温度，包含小数部分。 */
    float humidity;     /* 湿度，包含小数部分。 */
} dht11_reading_t;

/* 初始化 DHT11 读取模块。 */
esp_err_t dht11_rmt_init(gpio_num_t gpio_num);

/* 读取 DHT11 数据。 */
esp_err_t dht11_rmt_read(dht11_reading_t *data);


#endif