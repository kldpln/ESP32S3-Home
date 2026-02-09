#ifndef DHT11_H
#define DHT11_H

#include "esp_err.h"
#include <time.h>
#include <stdbool.h>
// 初始化 DHT11
void dht11_init(void);

// 启动 DHT11 读取任务
void dht11_start_task(void);

// 获取温度
int get_temperature_int(void);
int get_temperature_dec(void);

// 获取湿度
int get_humidity_int(void);
int get_humidity_dec(void);

//每天的数据结构
typedef struct  
{
    int weekday; // 0-6，表示周日到周六
    float max_temp;
    float min_temp;
    float max_hum;
    float min_hum;
    time_t timestamp; // 记录当天日期
    bool valid; // 标志位，表示数据是否有效
} DailyData;

//获取今日最大最小值
void get_today_stats(float *max_t, float *min_t, float *max_h, float *min_h);

//获取过去一周的历史数据
void get_weekly_history(DailyData* history_array);

#endif 