idf_component_register(
    SRCS "aj_sr04m_sensor.cpp" "ultrasonic_math.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board driver esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)