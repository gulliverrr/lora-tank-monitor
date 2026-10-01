idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES app_update battery blynk board config config_validation display lora protocol provisioning sensor storage tank
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)