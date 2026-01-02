#ifndef DHT11_H
#define DHT11_H

#include "esp_err.h"

// 初始化 DHT11
void dht11_init(void);

// 启动 DHT11 读取任务
void dht11_start_task(void);

// 获取温度（整数部分）
int get_temperature_int(void);

// 获取温度（小数部分）
int get_temperature_dec(void);

// 获取湿度（整数部分）
int get_humidity_int(void);

// 获取湿度（小数部分）
int get_humidity_dec(void);

#endif // DHT11_H