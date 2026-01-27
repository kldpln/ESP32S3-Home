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

// 获取统计数据 (Float)
float get_max_temp(void);
float get_min_temp(void);
float get_max_hum(void);
float get_min_hum(void);

float get_max_temp_yesterday(void);
float get_min_temp_yesterday(void);
float get_max_hum_yesterday(void);
float get_min_hum_yesterday(void);
#endif // DHT11_H