#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"
#include "system_monitor.h"

static const char *TAG = "SYS_MON";
static uint32_t monitor_interval_ms = 2000;

// 内部采集与打印任务
static void system_monitor_task(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        // uptime
        int64_t us = esp_timer_get_time();
        uint64_t uptime_ms = (us >= 0) ? (uint64_t)(us / 1000) : 0;

        // heap
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free_heap = esp_get_minimum_free_heap_size();
        size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t total_8bit = heap_caps_get_total_size(MALLOC_CAP_8BIT);
        size_t used_8bit = total_8bit > free_8bit ? (total_8bit - free_8bit) : 0;
        float mem_usage_pct = total_8bit > 0 ? (100.0f * (float)used_8bit) / (float)total_8bit : 0.0f;

        // tasks
        char *tasks_json = NULL;
        size_t tasks_json_len = 0;

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
        UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
        TaskStatus_t *task_status_array = pvPortMalloc(num_tasks * sizeof(TaskStatus_t));
        uint32_t total_run_time = 0;
        if (task_status_array != NULL) {
            UBaseType_t retrieved = uxTaskGetSystemState(task_status_array, num_tasks, &total_run_time);

            tasks_json_len = retrieved * 128 + 64;
            tasks_json = pvPortMalloc(tasks_json_len);
            if (tasks_json) {
                tasks_json[0] = '\0';
                strcat(tasks_json, "[");
                for (UBaseType_t i = 0; i < retrieved; i++) {
                    char entry[256];
                    const char *name = task_status_array[i].pcTaskName;
                    UBaseType_t high_water = task_status_array[i].usStackHighWaterMark;
                    uint32_t run_time = task_status_array[i].ulRunTimeCounter; // may be 0 if stats disabled
                    float pct = 0.0f;
                    if (total_run_time > 0) {
                        pct = (100.0f * (float)run_time) / (float)total_run_time;
                    }
                    char safe_name[configMAX_TASK_NAME_LEN];
                    strncpy(safe_name, name, sizeof(safe_name)-1);
                    safe_name[sizeof(safe_name)-1] = '\0';
                    for (char *p = safe_name; *p; ++p) if (*p == '"') *p = '\'';

                    snprintf(entry, sizeof(entry), "{\"name\":\"%s\",\"stack_high_water\":%u,\"run_time\":%u,\"run_pct\":%.2f},",
                             safe_name, (unsigned)high_water, (unsigned)run_time, pct);
                    if (strlen(tasks_json) + strlen(entry) + 2 < tasks_json_len) {
                        strcat(tasks_json, entry);
                    }
                }
                size_t len = strlen(tasks_json);
                if (len > 1 && tasks_json[len-1] == ',') tasks_json[len-1] = '\0';
                strcat(tasks_json, "]");
            }
            vPortFree(task_status_array);
        }
#else
        // Trace facility not enabled: provide basic task count but no per-task run-time stats
        tasks_json_len = 64;
        tasks_json = pvPortMalloc(tasks_json_len);
        if (tasks_json) {
            snprintf(tasks_json, tasks_json_len, "null");
        }
#endif

        // Compose full JSON line
        // Note: use printf to get a clean line without ESP_LOG prefix
        if (tasks_json) {
                 printf("{\"uptime_ms\":%llu,\"free_heap\":%u,\"min_free_heap\":%u,\"free_8bit\":%u,\"total_heap_8bit\":%u,\"used_heap_8bit\":%u,\"mem_usage_pct\":%.2f,\"tasks\":%s}\n",
                     (unsigned long long)uptime_ms, (unsigned)free_heap, (unsigned)min_free_heap, (unsigned)free_8bit,
                     (unsigned)total_8bit, (unsigned)used_8bit, mem_usage_pct, tasks_json);
            vPortFree(tasks_json);
        } else {
                 printf("{\"uptime_ms\":%llu,\"free_heap\":%u,\"min_free_heap\":%u,\"free_8bit\":%u,\"total_heap_8bit\":%u,\"used_heap_8bit\":%u,\"mem_usage_pct\":%.2f,\"tasks\":null}\n",
                     (unsigned long long)uptime_ms, (unsigned)free_heap, (unsigned)min_free_heap, (unsigned)free_8bit,
                     (unsigned)total_8bit, (unsigned)used_8bit, mem_usage_pct);
        }

        vTaskDelay(pdMS_TO_TICKS(monitor_interval_ms));
    }
}

void system_monitor_init(void)
{
    // nothing to init for now
}

void system_monitor_start_task(uint32_t interval_ms)
{
    if (interval_ms > 0) monitor_interval_ms = interval_ms;
    xTaskCreatePinnedToCore(system_monitor_task, "system_monitor", 4096, NULL, 5, NULL, 1);
}
