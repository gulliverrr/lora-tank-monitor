idf_component_register(
    SRCS "blynk_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config esp_event esp_netif esp-tls esp_wifi mbedtls nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)