idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES Webserver AP DataProcess esp_event esp_netif nvs_flash esp_wifi esp_timer driver log)