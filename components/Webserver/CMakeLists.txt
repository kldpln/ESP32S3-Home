idf_component_register(SRCS "web.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_server log DHT11)

# 嵌入 JPG 图片
target_add_binary_data(${COMPONENT_TARGET} "back.jpg" BINARY)
# 嵌入 HTML
target_add_binary_data(${COMPONENT_TARGET} "index.html" TEXT)
#update
