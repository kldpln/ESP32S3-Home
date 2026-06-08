#pragma once
#include <stdint.h>

/* 初始化系统监测模块，启动时调用一次。 */
void system_monitor_init(void);

/* 启动系统监测任务。 */
void system_monitor_start_task(uint32_t interval_ms);
