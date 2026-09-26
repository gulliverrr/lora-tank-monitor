idf_component_register(
    SRCS "config_form.cpp" "provisioning_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config config_validation esp_event esp_http_server esp_netif esp_wifi json lwip nvs_flash storage
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)