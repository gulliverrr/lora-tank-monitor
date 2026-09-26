idf_component_register(
    SRCS "protocol_codec.cpp" "sequence_tracker.cpp"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)