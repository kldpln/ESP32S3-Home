#pragma once
#include <stdint.h>

// Initialize system monitor (call once at startup)
void system_monitor_init(void);

// Start system monitor task (creates a FreeRTOS task)
void system_monitor_start_task(uint32_t interval_ms);
