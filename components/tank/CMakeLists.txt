idf_component_register(
    SRCS "tank_calculator.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)