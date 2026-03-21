idf_component_register(SRCS "data_process.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_timer nvs_flash RMT)