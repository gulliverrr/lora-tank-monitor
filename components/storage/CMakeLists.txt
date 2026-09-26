idf_component_register(
    SRCS "config_codec.cpp" "config_store.cpp" "record_selection.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)