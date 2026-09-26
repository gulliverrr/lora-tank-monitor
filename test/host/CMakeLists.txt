cmake_minimum_required(VERSION 3.16)
project(lora_tank_monitor_host_tests LANGUAGES CXX)

enable_testing()

add_executable(tank_calculator_tests
    tank_calculator_tests.cpp
    ../../components/tank/tank_calculator.cpp
)
target_compile_features(tank_calculator_tests PRIVATE cxx_std_17)
target_compile_options(tank_calculator_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(tank_calculator_tests PRIVATE
    ../../components/config/include
    ../../components/tank/include
)

add_test(NAME tank_calculator COMMAND tank_calculator_tests)

add_executable(alarm_state_machine_tests
    alarm_state_machine_tests.cpp
    ../../components/alarms/alarm_state_machine.cpp
)
target_compile_features(alarm_state_machine_tests PRIVATE cxx_std_17)
target_compile_options(alarm_state_machine_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(alarm_state_machine_tests PRIVATE
    ../../components/config/include
    ../../components/alarms/include
)

add_test(NAME alarm_state_machine COMMAND alarm_state_machine_tests)

add_executable(protocol_tests
    protocol_tests.cpp
    ../../components/protocol/protocol_codec.cpp
    ../../components/protocol/sequence_tracker.cpp
)
target_compile_features(protocol_tests PRIVATE cxx_std_17)
target_compile_options(protocol_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(protocol_tests PRIVATE ../../components/protocol/include)

add_test(NAME protocol COMMAND protocol_tests)

add_executable(config_validation_tests
    config_validation_tests.cpp
    ../../components/config/app_config.cpp
    ../../components/config_validation/config_validator.cpp
    ../../components/tank/tank_calculator.cpp
    ../../components/alarms/alarm_state_machine.cpp
)
target_compile_features(config_validation_tests PRIVATE cxx_std_17)
target_compile_options(config_validation_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(config_validation_tests PRIVATE
    ../../components/config/include
    ../../components/config_validation/include
    ../../components/tank/include
    ../../components/alarms/include
)

add_test(NAME config_validation COMMAND config_validation_tests)

add_executable(ultrasonic_math_tests
    ultrasonic_math_tests.cpp
    ../../components/sensor/ultrasonic_math.cpp
)
target_compile_features(ultrasonic_math_tests PRIVATE cxx_std_17)
target_compile_options(ultrasonic_math_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(ultrasonic_math_tests PRIVATE ../../components/sensor/include)

add_test(NAME ultrasonic_math COMMAND ultrasonic_math_tests)

add_executable(battery_estimator_tests
    battery_estimator_tests.cpp
    ../../components/battery/battery_estimator.cpp
    ../../components/config/app_config.cpp
)
target_compile_features(battery_estimator_tests PRIVATE cxx_std_17)
target_compile_options(battery_estimator_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(battery_estimator_tests PRIVATE
    ../../components/battery/include
    ../../components/config/include
)

add_test(NAME battery_estimator COMMAND battery_estimator_tests)

add_executable(config_codec_tests
    config_codec_tests.cpp
    ../../components/storage/config_codec.cpp
    ../../components/config/app_config.cpp
)
target_compile_features(config_codec_tests PRIVATE cxx_std_17)
target_compile_options(config_codec_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(config_codec_tests PRIVATE
    ../../components/storage/include
    ../../components/config/include
)

add_test(NAME config_codec COMMAND config_codec_tests)

add_executable(record_selection_tests
    record_selection_tests.cpp
    ../../components/storage/record_selection.cpp
)
target_compile_features(record_selection_tests PRIVATE cxx_std_17)
target_compile_options(record_selection_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(record_selection_tests PRIVATE ../../components/storage/include)

add_test(NAME record_selection COMMAND record_selection_tests)

add_executable(config_migration_tests
    config_migration_tests.cpp
    ../../components/config/app_config.cpp
    ../../components/config/config_migration.cpp
)
target_compile_features(config_migration_tests PRIVATE cxx_std_17)
target_compile_options(config_migration_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(config_migration_tests PRIVATE ../../components/config/include)

add_test(NAME config_migration COMMAND config_migration_tests)

add_executable(config_form_tests
    config_form_tests.cpp
    ../../components/provisioning/config_form.cpp
    ../../components/config/app_config.cpp
    ../../components/config_validation/config_validator.cpp
    ../../components/tank/tank_calculator.cpp
    ../../components/alarms/alarm_state_machine.cpp
)
target_compile_features(config_form_tests PRIVATE cxx_std_17)
target_compile_options(config_form_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(config_form_tests PRIVATE
    ../../components/provisioning/include
    ../../components/config/include
    ../../components/config_validation/include
    ../../components/tank/include
    ../../components/alarms/include
)

add_test(NAME config_form COMMAND config_form_tests)