# SystemMonitor code is kept in the repository, but it is disabled by default
# and will not be compiled unless this guard is removed intentionally.
return()

idf_component_register(SRCS "system_monitor.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos esp_timer esp_system
)
