#ifndef DATA_PROCESS_H
#define DATA_PROCESS_H

#include "esp_err.h"
#include <time.h>
#include <stdbool.h>
/* 初始化 DHT11 采样模块。 */
void data_process_init(void);

/* 启动 DHT11 读取任务。 */
void data_process_start_task(void);

/* 获取温度整数部分。 */
int get_temperature_int(void);
/* 获取温度小数部分。 */
int get_temperature_dec(void);

/* 获取湿度整数部分。 */
int get_humidity_int(void);
/* 获取湿度小数部分。 */
int get_humidity_dec(void);

/* 获取当前采样是否有效。 */
bool get_current_reading_valid(void);

/* 获取当前原始温湿度值。 */
float get_current_temperature(void);
float get_current_humidity(void);

/* 每日统计数据结构。 */
typedef struct  
{
    int weekday; /* 0-6，表示周日到周六。 */
    float max_temp;
    float min_temp;
    float max_hum;
    float min_hum;
    time_t timestamp; /* 当天时间戳。 */
    bool valid; /* 数据有效标志。 */
} DailyData;

/* 获取当日温湿度极值。 */
void get_today_stats(float *max_t, float *min_t, float *max_h, float *min_h);

/* 获取最近 7 天历史数据。 */
void get_weekly_history(DailyData* history_array);

#endif 