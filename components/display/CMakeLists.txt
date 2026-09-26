idf_component_register(
    SRCS "display_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES battery board chill-sam__ssd1306 esp_timer freertos
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)