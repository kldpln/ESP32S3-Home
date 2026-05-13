idf_component_register(SRCS "system_monitor.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos esp_timer esp_system
)
