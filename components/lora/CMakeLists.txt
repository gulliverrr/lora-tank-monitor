idf_component_register(
    SRCS "esp_radio_hal.cpp" "lora_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board config driver esp_timer jgromes__radiolib protocol
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)