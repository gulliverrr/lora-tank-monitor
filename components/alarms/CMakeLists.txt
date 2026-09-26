idf_component_register(
    SRCS "alarm_state_machine.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)