idf_component_register(
    SRCS "battery_estimator.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)