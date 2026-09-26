idf_component_register(
    SRCS "board.cpp" "power_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES cube32esp__xpowerslib driver esp_hw_support esp_system spi_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)