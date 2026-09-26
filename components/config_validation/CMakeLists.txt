idf_component_register(
    SRCS "config_validator.cpp"
    INCLUDE_DIRS "include"
    REQUIRES alarms config tank
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)