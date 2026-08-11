# Helper: create a test executable linked against hal_mock
function(add_hal_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE hal_mock)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

function(add_hal_fuzzer NAME)
    add_executable(${NAME} fuzz/${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE hal_mock)
    target_compile_options(${NAME} PRIVATE -fsanitize=fuzzer)
    target_link_options(${NAME} PRIVATE -fsanitize=fuzzer)
    jh_enable_host_warnings(${NAME} ERRORS)
endfunction()

function(add_hal_freertos_posix_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE hal_freertos_posix)
    add_test(NAME ${NAME} COMMAND ${NAME})
    set_tests_properties(${NAME} PROPERTIES
        LABELS "freertos_posix;no_memcheck"
        TIMEOUT 20)
endfunction()

function(add_platform_disabled_test NAME REASON LABEL)
    add_test(NAME ${NAME}
        COMMAND ${CMAKE_COMMAND} -E echo "${NAME}: ${REASON}")
    set_tests_properties(${NAME} PROPERTIES
        DISABLED TRUE
        LABELS "${LABEL}")
endfunction()

add_test(NAME test_host_warning_profiles
    COMMAND ${CMAKE_COMMAND}
        "-DJH_ROOT=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/test_host_warning_profiles.cmake")

# Keep the native compiler gate independent from the monolithic mock backend.
# Windows firmware support does not imply that every GNU-oriented host mock is
# portable to MSVC; this target still compiles and runs real HAL code under the
# strict warning profile.
add_executable(test_host_compiler_smoke
    test_host_compiler_smoke.cpp
    test_host_compiler_portable.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/security/hal_crc.cpp)
target_compile_definitions(test_host_compiler_smoke PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_CRC)
# One translation unit forces the portable fallback, so the branch no real
# compiler selects is compiled and compared against this compiler's builtins.
set_source_files_properties(test_host_compiler_portable.cpp PROPERTIES
    COMPILE_DEFINITIONS "HAL_COMPILER_IS_GNU_LIKE=0;HAL_COMPILER_IS_MSVC=0")
target_include_directories(test_host_compiler_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
jh_enable_host_warnings(test_host_compiler_smoke ERRORS)
add_test(NAME test_host_compiler_smoke COMMAND test_host_compiler_smoke)

# These public headers must remain standalone in both supported source
# languages. They intentionally do not link the monolithic mock backend.
add_executable(test_hal_config_headers_c test_hal_config_headers_c.c)
target_include_directories(test_hal_config_headers_c PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
jh_enable_host_warnings(test_hal_config_headers_c ERRORS)
add_test(NAME test_hal_config_headers_c COMMAND test_hal_config_headers_c)

add_executable(test_hal_config_headers_cpp test_hal_config_headers.cpp)
target_include_directories(test_hal_config_headers_cpp PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
jh_enable_host_warnings(test_hal_config_headers_cpp ERRORS)
add_test(NAME test_hal_config_headers_cpp COMMAND test_hal_config_headers_cpp)

add_executable(test_hal_lora_radio_header_c test_hal_lora_radio_header_c.c)
target_compile_definitions(test_hal_lora_radio_header_c PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_SX126X=1)
target_include_directories(test_hal_lora_radio_header_c PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
jh_enable_host_warnings(test_hal_lora_radio_header_c ERRORS)
add_test(NAME test_hal_lora_radio_header_c COMMAND test_hal_lora_radio_header_c)

add_executable(test_hal_lora_radio_header_cpp test_hal_lora_radio_header.cpp)
target_compile_definitions(test_hal_lora_radio_header_cpp PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_SX127X=1)
target_include_directories(test_hal_lora_radio_header_cpp PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
jh_enable_host_warnings(test_hal_lora_radio_header_cpp ERRORS)
add_test(NAME test_hal_lora_radio_header_cpp
    COMMAND test_hal_lora_radio_header_cpp)

# The public socket declaration surface is portable and compiled by both MSVC
# and GNU-like host gates. The full adapter intentionally exports POSIX symbol
# names and remains a firmware/Linux-host contract rather than a Winsock ABI.
add_executable(test_bsd_socket_headers_c test_bsd_socket_headers_c.c)
target_include_directories(test_bsd_socket_headers_c PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
jh_enable_host_warnings(test_bsd_socket_headers_c ERRORS)
add_test(NAME test_bsd_socket_headers_c COMMAND test_bsd_socket_headers_c)

function(add_stm32_host_test NAME)
    set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    add_executable(${NAME}
        ${NAME}.cpp
        ${SRC_DIR}/hal/impl/stm32g474/hal_timer.cpp
        ${SRC_DIR}/hal/timers/hal_timer_ext.cpp
        ${SRC_DIR}/hal/impl/stm32g474/hal_sync.cpp
        ${SRC_DIR}/hal/impl/stm32g474/hal_system.cpp
        ${SRC_DIR}/hal/impl/stm32g474/drivers/stm32g474/stm32g474_system.cpp
        ${SRC_DIR}/hal/impl/stm32g474/drivers/stm32g474/stm32g474_fault.cpp
        ${SRC_DIR}/hal/core/hal_assert.cpp
        ${SRC_DIR}/hal/core/hal_config.cpp
        ${SRC_DIR}/utils/unity.c
    )
    target_include_directories(${NAME} PRIVATE
        ${SRC_DIR}
    )
    target_compile_definitions(${NAME} PRIVATE
        HAL_TARGET_STM32G474=1
        UNITY_INCLUDE_CONFIG_H
        HAL_ENABLE_UNITY
    )
    jh_enable_host_warnings(${NAME} ERRORS)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

function(add_stm32_i2c_slave_host_test NAME)
    set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    add_executable(${NAME}
        ${NAME}.cpp
        ${SRC_DIR}/hal/impl/stm32g474/hal_i2c_slave.cpp
        ${SRC_DIR}/hal/impl/stm32g474/hal_sync.cpp
        ${SRC_DIR}/hal/core/hal_assert.cpp
        ${SRC_DIR}/hal/core/hal_config.cpp
        ${SRC_DIR}/utils/unity.c
    )
    target_include_directories(${NAME} PRIVATE
        ${SRC_DIR}
    )
    target_compile_definitions(${NAME} PRIVATE
        HAL_TARGET_STM32G474=1
        HAL_ENABLE_I2C_SLAVE
        UNITY_INCLUDE_CONFIG_H
        HAL_ENABLE_UNITY
    )
    jh_enable_host_warnings(${NAME} ERRORS)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

function(add_stm32_gpio_host_test NAME)
    set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    add_executable(${NAME}
        ${NAME}.cpp
        ${SRC_DIR}/hal/impl/stm32g474/hal_gpio.cpp
        ${SRC_DIR}/hal/core/hal_assert.cpp
        ${SRC_DIR}/hal/core/hal_config.cpp
        ${SRC_DIR}/utils/unity.c
    )
    target_include_directories(${NAME} PRIVATE
        ${SRC_DIR}
    )
    target_compile_definitions(${NAME} PRIVATE
        HAL_TARGET_STM32G474=1
        UNITY_INCLUDE_CONFIG_H
        HAL_ENABLE_UNITY
    )
    jh_enable_host_warnings(${NAME})
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_executable(test_rp2040_adc_shared
    test_rp2040_adc_shared.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/rp2040/rp2040_adc_shared.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_rp2040_adc_shared PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes/rp2040_adc_sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_rp2040_adc_shared PRIVATE
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_rp2040_adc_shared ERRORS)
target_link_libraries(test_rp2040_adc_shared PRIVATE Threads::Threads)
add_test(NAME test_rp2040_adc_shared COMMAND test_rp2040_adc_shared)

# Exercise the real Fiesta-convention CMake generator, not a handwritten copy.
# The secondary initializer must execute on the first native core-1 task call.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/jh_entry_adapter.cmake")
set(_entry_fixture_dir
    "${CMAKE_CURRENT_BINARY_DIR}/jh_entry_adapter_fixture")
set(_entry_generated_dir
    "${CMAKE_CURRENT_BINARY_DIR}/jh_entry_adapter_generated")
file(MAKE_DIRECTORY "${_entry_fixture_dir}")
file(WRITE "${_entry_fixture_dir}/firmware_entry.h" [=[
#pragma once
#define FIESTA_ENABLE_CORE1 1

#ifdef __cplusplus
extern "C" {
#endif
void initialization(void);
void initialization1(void);
void looper(void);
void looper1(void);
#ifdef __cplusplus
}
#endif
]=])

jh_generate_entry_adapter(
    "${_entry_fixture_dir}"
    "${_entry_generated_dir}"
    _entry_adapter_source
    _entry_adapter_has_core1
)
if(NOT _entry_adapter_source OR NOT _entry_adapter_has_core1)
    message(FATAL_ERROR "Core-1 entry adapter fixture was not generated")
endif()
jh_validate_entry_adapter_features(
    "${_entry_adapter_has_core1}" HAL_ENABLE_APP_TASK1)

add_executable(test_jh_entry_adapter
    test_jh_entry_adapter.cpp
    "${_entry_adapter_source}"
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_jh_entry_adapter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    "${_entry_fixture_dir}"
)
target_compile_definitions(test_jh_entry_adapter PRIVATE
    HAL_TARGET_RP2040=1
    HAL_BOARD_PROFILE_RP_PICO=1
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_jh_entry_adapter)
add_test(NAME test_jh_entry_adapter COMMAND test_jh_entry_adapter)

add_executable(test_lwip_raw_engines
    test_lwip_raw_engines.cpp
    fakes/lwip_fake.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/jh_lwip_udp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/jh_lwip_tcp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_lwip_raw_engines PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_lwip_raw_engines PRIVATE
    HAL_TARGET_MOCK=1
    JH_LWIP_RAW_TEST=1
    HAL_LWIP_UDP_RX_QUEUE_DEPTH=2
    HAL_LWIP_UDP_MAX_PAYLOAD=8
    HAL_LWIP_TCP_RX_LIMIT=8
    HAL_LWIP_TCP_ACCEPT_QUEUE_DEPTH=2
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_lwip_raw_engines ERRORS)
add_test(NAME test_lwip_raw_engines COMMAND test_lwip_raw_engines)

add_executable(test_network_service
    test_network_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/jh_network_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_network_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_network_service PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_network_service ERRORS)
add_test(NAME test_network_service COMMAND test_network_service)

add_executable(test_cyw43_radio_runtime
    test_cyw43_radio_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/cyw43/jh_cyw43_radio_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/jh_network_service.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_cyw43_radio_runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_cyw43_radio_runtime PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_cyw43_radio_runtime ERRORS)
add_test(NAME test_cyw43_radio_runtime COMMAND test_cyw43_radio_runtime)

add_executable(test_ble_hci_transport
    test_ble_hci_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/bluetooth/jh_ble_hci_transport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_ble_hci_transport PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_ble_hci_transport PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_ble_hci_transport ERRORS)
add_test(NAME test_ble_hci_transport COMMAND test_ble_hci_transport)

add_executable(test_network_backend
    test_network_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/core/jh_handle_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/jh_network_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/jh_network_handle_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_network_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_network_backend PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_WIFI
    HAL_ENABLE_TCP
    HAL_ENABLE_UDP
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_network_backend ERRORS)
add_test(NAME test_network_backend COMMAND test_network_backend)

add_executable(test_network_backend_contract_host
    test_network_backend_contract.cpp
    fakes/network_contract_host_adapter.cpp
)
target_link_libraries(test_network_backend_contract_host PRIVATE hal_mock)
target_include_directories(test_network_backend_contract_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
jh_enable_host_warnings(test_network_backend_contract_host ERRORS)
add_test(NAME test_network_backend_contract_host
    COMMAND test_network_backend_contract_host
)

add_executable(test_network_backend_contract_offload
    test_network_backend_contract.cpp
    fakes/network_contract_offload_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/core/jh_handle_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/jh_network_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/jh_network_handle_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_network_backend_contract_offload PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_network_backend_contract_offload PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_WIFI
    HAL_ENABLE_TCP
    HAL_ENABLE_UDP
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_network_backend_contract_offload ERRORS)
add_test(NAME test_network_backend_contract_offload
    COMMAND test_network_backend_contract_offload
)

add_test(NAME test_network_backend_selection
    COMMAND ${CMAKE_COMMAND}
        -DJH_CXX=${CMAKE_CXX_COMPILER}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_network_backend_selection.cmake
)

add_test(NAME test_target_selection
    COMMAND ${CMAKE_COMMAND}
        -DJH_CXX=${CMAKE_CXX_COMPILER}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_target_selection.cmake
)

add_test(NAME test_board_selection
    COMMAND ${CMAKE_COMMAND}
        -DJH_CXX=${CMAKE_CXX_COMPILER}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_board_selection.cmake
)

add_test(NAME test_project_feature_defines
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -DJH_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_project_feature_defines.cmake
)

add_test(NAME test_rp_native_build_contract
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_rp_native_build_contract.cmake
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME test_feature_registry
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_feature_registry.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        --cc ${CMAKE_C_COMPILER}
        --cmake ${CMAKE_COMMAND}
)
add_test(NAME test_feature_registry_cmake
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_feature_registry.cmake
)
add_test(NAME test_vscode_native_workflow
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_vscode_native_workflow.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_vscode_native_upload
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_vscode_native_upload.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_vscode_serial_persistent
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_vscode_serial_persistent.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_vscode_platform_adapter
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_vscode_platform_adapter.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_vscode_windows_tooling
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_vscode_windows_tooling.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_vscode_windows_serial
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_vscode_windows_serial.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_vscode_windows_bootsel
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_vscode_windows_bootsel.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_vscode_build_environment
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_vscode_build_environment.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_build_artifact_layout
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_build_artifact_layout.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_ci_library_matrix
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_ci_library_matrix.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_stm32_host_sanity_build
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_stm32_host_sanity_build.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_rp_ota_artifacts
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_rp_ota_artifacts.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_component_management
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_component_management.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_bluetooth_stage1
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_bluetooth_stage1.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_component_manager
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_component_manager.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_cpd_gate
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_cpd_gate.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_ota_firewall_config
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_ota_firewall_config.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_serial_io
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_serial_io.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_board_generator
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_board_generator.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_documentation_links
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check_documentation_links.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_release_metadata
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_release_metadata.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME test_sbom_inventory
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_sbom_inventory.py
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_executable(test_hal_board_runtime
    test_hal_board_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/system/hal_board.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_hal_board_runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_hal_board_runtime PRIVATE
    HAL_TARGET_RP2040=1
    HAL_BOARD_PROFILE_RP_PICO_W=1
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_hal_board_runtime ERRORS)
add_test(NAME test_hal_board_runtime COMMAND test_hal_board_runtime)

function(add_network_board_runtime_test NAME BOARD_DEFINE TEST_DEFINE)
    set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    add_executable(${NAME}
        test_network_board_runtime.cpp
        ${SRC_DIR}/hal/system/hal_board.cpp
        ${SRC_DIR}/hal/network/hal_net.cpp
        ${SRC_DIR}/hal/network/hal_tcp.cpp
        ${SRC_DIR}/hal/network/hal_udp.cpp
        ${SRC_DIR}/hal/network/hal_wifi.cpp
        ${SRC_DIR}/hal/core/jh_handle_pool.cpp
        ${SRC_DIR}/hal/network/jh_network_backend.cpp
        ${SRC_DIR}/hal/network/jh_network_handle_pool.cpp
        ${SRC_DIR}/utils/unity.c
    )
    target_include_directories(${NAME} PRIVATE ${SRC_DIR})
    target_compile_definitions(${NAME} PRIVATE
        HAL_TARGET_RP2040=1
        ${BOARD_DEFINE}=1
        ${TEST_DEFINE}=1
        HAL_ENABLE_WIFI=1
        HAL_ENABLE_TCP=1
        HAL_ENABLE_UDP=1
        JH_HAL_UDP_SOCKET_API_ONLY=1
        HAL_NETWORK_BACKEND_CYW43=1
        HAL_CYW43_BUS_PICO_PIO=1
        HAL_CYW43_STACK_LWIP=1
        HAL_CYW43_MAX_TRANSACTION_BYTES=2048u
        HAL_ENABLE_UNITY
        UNITY_INCLUDE_CONFIG_H
    )
    jh_enable_host_warnings(${NAME} ERRORS)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

add_network_board_runtime_test(
    test_network_board_runtime_picow
    HAL_BOARD_PROFILE_RP_PICO_W
    JH_TEST_NETWORK_BOARD_PICOW
)
add_network_board_runtime_test(
    test_network_board_runtime_pim730
    HAL_BOARD_PROFILE_RP_PICO_PIM730
    JH_TEST_NETWORK_BOARD_PIM730
)

add_library(test_hal_http_client_plaintext_compile OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/http/hal_http_client.cpp
)
target_include_directories(test_hal_http_client_plaintext_compile PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_hal_http_client_plaintext_compile PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_HTTP_CLIENT=1
)
jh_enable_host_warnings(test_hal_http_client_plaintext_compile ERRORS)

add_executable(test_pubsub_hal_client
    test_pubsub_hal_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/mqtt/jh_pubsub_hal_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/mqtt/PubSubClient/src/PubSubClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_pubsub_hal_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_pubsub_hal_client PRIVATE
    HAL_TARGET_RP2040=1
    HAL_NETWORK_BACKEND_CYW43=1
    HAL_CYW43_BUS_PICO_PIO=1
    HAL_CYW43_STACK_LWIP=1
    HAL_CYW43_PROFILE_PICOW=1
    HAL_CYW43_MAX_TRANSACTION_BYTES=2048u
    HAL_ENABLE_MQTT
    HAL_ENABLE_TLS
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_pubsub_hal_client ERRORS)
add_test(NAME test_pubsub_hal_client COMMAND test_pubsub_hal_client)

add_executable(test_wireguard_lwip_port
    test_wireguard_lwip_port.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/jh_lwip_extension.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/wireguard/core/jh_wireguard_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_wireguard_lwip_port BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_wireguard_lwip_port PRIVATE
    HAL_TARGET_RP2040=1
    HAL_NETWORK_BACKEND_CYW43=1
    HAL_CYW43_BUS_PICO_PIO=1
    HAL_CYW43_STACK_LWIP=1
    HAL_CYW43_PROFILE_PICOW=1
    HAL_CYW43_MAX_TRANSACTION_BYTES=2048u
    HAL_ENABLE_WIREGUARD
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_wireguard_lwip_port ERRORS)
add_test(NAME test_wireguard_lwip_port COMMAND test_wireguard_lwip_port)

# RP2040 SoftwareSerial must stay on the native Pico SDK PIO backend.  This is a
# source-selection contract rather than a host runtime test: the regression it
# guards against is compiling the portable, GPIO-ISR bit-banger for RP2040.
add_test(NAME test_rp2040_swserial_backend
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_rp2040_swserial_backend.cmake
)

add_test(NAME test_cyw43_dependency_boundary
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_cyw43_dependency_boundary.cmake
)

add_test(NAME test_wireguard_dependency_boundary
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_wireguard_dependency_boundary.cmake
)

add_test(NAME test_lwip_dependency_boundary
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_lwip_dependency_boundary.cmake
)

add_test(NAME test_calendar_architecture
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_calendar_architecture.cmake
)

add_test(NAME test_time_ntp_architecture
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_time_ntp_architecture.cmake
)

add_test(NAME test_rtc_architecture
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_rtc_architecture.cmake
)
add_test(NAME test_eeprom_architecture
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_eeprom_architecture.cmake
)
add_test(NAME test_thermocouple_architecture
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_thermocouple_architecture.cmake
)

add_test(NAME test_gps_architecture
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_gps_architecture.cmake
)

add_test(NAME test_security_architecture
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_security_architecture.cmake
)

add_test(NAME test_serial_architecture
    COMMAND ${CMAKE_COMMAND}
        -DJH_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test_serial_architecture.cmake
)

get_filename_component(_jh_workspace_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
    ABSOLUTE)
set(JH_CW43_FIXTURE_SETUP_SCRIPT
    "${_jh_workspace_root}/context-providers/context_JaszczurHAL/cw43_test_host_setup.sh"
    CACHE FILEPATH "Optional external CW43 hardware-fixture setup script")
if(EXISTS "${JH_CW43_FIXTURE_SETUP_SCRIPT}")
    add_test(NAME test_cw43_fixture_build_identity
        COMMAND ${CMAKE_COMMAND}
            "-DJH_CW43_FIXTURE_SETUP_SCRIPT=${JH_CW43_FIXTURE_SETUP_SCRIPT}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/test_cw43_fixture_build_identity.cmake
    )
else()
    message(STATUS
        "External CW43 fixture not found; fixture identity test is not registered")
endif()

add_hal_test(test_hal_gpio)
add_hal_test(test_cyw43_config)
add_hal_test(test_rp2040_cyw43_gspi_clock)
add_hal_test(test_cyw43_driver)
add_hal_test(test_cyw43_gspi_transport)
add_hal_test(test_cyw43_lwip)
add_hal_test(test_hal_crc)
add_hal_test(test_hal_can)
add_hal_test(test_hal_thermocouple)
add_hal_test(test_hal_critical_section)
add_executable(test_pcf8563_driver
    test_pcf8563_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/rtc/pcf8563/pcf8563.cpp
)
target_link_libraries(test_pcf8563_driver PRIVATE hal_mock)
add_test(NAME test_pcf8563_driver COMMAND test_pcf8563_driver)
add_executable(test_ds3231_driver
    test_ds3231_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/rtc/ds3231/ds3231.cpp
)
target_link_libraries(test_ds3231_driver PRIVATE hal_mock)
add_test(NAME test_ds3231_driver COMMAND test_ds3231_driver)
add_executable(test_jh_rtc_i2c_provider
    test_jh_rtc_i2c_provider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/rtc/jh_rtc_i2c_provider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/rtc/pcf8563/pcf8563.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/rtc/ds3231/ds3231.cpp
)
target_link_libraries(test_jh_rtc_i2c_provider PRIVATE hal_mock)
add_test(NAME test_jh_rtc_i2c_provider COMMAND test_jh_rtc_i2c_provider)
add_executable(test_max6675_driver
    test_max6675_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/temperature/max6675/max6675_driver.cpp
)
target_link_libraries(test_max6675_driver PRIVATE hal_mock)
add_test(NAME test_max6675_driver COMMAND test_max6675_driver)
add_executable(test_mcp9600_driver
    test_mcp9600_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/temperature/mcp9600/mcp9600_driver.cpp
)
target_link_libraries(test_mcp9600_driver PRIVATE hal_mock)
add_test(NAME test_mcp9600_driver COMMAND test_mcp9600_driver)
add_executable(test_bh1750_driver
    test_bh1750_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/sensors/bh1750/hal_bh1750.cpp
)
target_link_libraries(test_bh1750_driver PRIVATE hal_mock)
add_test(NAME test_bh1750_driver COMMAND test_bh1750_driver)
add_executable(test_adp5360_driver
    test_adp5360_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/power/adp5360/hal_adp5360.cpp
)
target_compile_definitions(test_adp5360_driver PRIVATE HAL_ENABLE_ADP5360)
target_link_libraries(test_adp5360_driver PRIVATE hal_mock)
add_test(NAME test_adp5360_driver COMMAND test_adp5360_driver)
add_hal_test(test_simple_io_drivers)
add_executable(test_hd44780_driver
    test_hd44780_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/hd44780/hd44780.cpp
)
target_compile_definitions(test_hd44780_driver PRIVATE HAL_ENABLE_HD44780)
target_link_libraries(test_hd44780_driver PRIVATE hal_mock)
add_test(NAME test_hd44780_driver COMMAND test_hd44780_driver)
add_executable(test_tsc2007_driver
    test_tsc2007_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/input/tsc2007/tsc2007.cpp
)
target_compile_definitions(test_tsc2007_driver PRIVATE HAL_ENABLE_TSC2007)
target_link_libraries(test_tsc2007_driver PRIVATE hal_mock)
add_test(NAME test_tsc2007_driver COMMAND test_tsc2007_driver)
add_executable(test_stmpe610_driver
    test_stmpe610_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/input/stmpe610/stmpe610.cpp
)
target_compile_definitions(test_stmpe610_driver PRIVATE HAL_ENABLE_STMPE610)
target_link_libraries(test_stmpe610_driver PRIVATE hal_mock)
add_test(NAME test_stmpe610_driver COMMAND test_stmpe610_driver)
add_executable(test_irsmall_decoder_driver
    test_irsmall_decoder_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/input/irsmall_decoder/irsmall_decoder.cpp
)
target_compile_definitions(test_irsmall_decoder_driver PRIVATE HAL_ENABLE_IRSMALL_DECODER)
target_link_libraries(test_irsmall_decoder_driver PRIVATE hal_mock)
add_test(NAME test_irsmall_decoder_driver COMMAND test_irsmall_decoder_driver)
add_executable(test_ads1x15_driver
    test_ads1x15_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/analog/ads1x15/ads1x15_driver.cpp
)
target_link_libraries(test_ads1x15_driver PRIVATE hal_mock)
add_test(NAME test_ads1x15_driver COMMAND test_ads1x15_driver)
add_hal_test(test_dacless_driver)
add_executable(test_ili9341_driver
    test_ili9341_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/ili9341_driver.cpp
)
target_link_libraries(test_ili9341_driver PRIVATE hal_mock)
add_test(NAME test_ili9341_driver COMMAND test_ili9341_driver)
add_executable(test_st77xx_driver
    test_st77xx_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/st77xx_driver.cpp
)
target_compile_definitions(test_st77xx_driver PRIVATE
    HAL_ENABLE_ST7735
    HAL_ENABLE_ST7789
    HAL_ENABLE_ST7796S
    HAL_ENABLE_GC9A01
)
target_link_libraries(test_st77xx_driver PRIVATE hal_mock)
add_test(NAME test_st77xx_driver COMMAND test_st77xx_driver)
add_executable(test_ssd1306_driver
    test_ssd1306_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/ssd1306_driver.cpp
)
target_link_libraries(test_ssd1306_driver PRIVATE hal_mock)
add_test(NAME test_ssd1306_driver COMMAND test_ssd1306_driver)
add_executable(test_rgb_oled_driver
    test_rgb_oled_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/rgb_oled_driver.cpp
)
target_compile_definitions(test_rgb_oled_driver PRIVATE
    HAL_ENABLE_SSD1331
    HAL_ENABLE_SSD135X
)
target_link_libraries(test_rgb_oled_driver PRIVATE hal_mock)
add_test(NAME test_rgb_oled_driver COMMAND test_rgb_oled_driver)
add_executable(test_st7567_driver
    test_st7567_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/st7567_driver.cpp
)
target_compile_definitions(test_st7567_driver PRIVATE HAL_ENABLE_ST7567)
target_link_libraries(test_st7567_driver PRIVATE hal_mock)
add_test(NAME test_st7567_driver COMMAND test_st7567_driver)

add_executable(test_epd_drivers
    test_epd_drivers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/epd_spi_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/ssd16xx_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/uc81xx_driver.cpp
)
target_compile_definitions(test_epd_drivers PRIVATE
    HAL_ENABLE_SSD16XX
    HAL_ENABLE_UC81XX
)
target_link_libraries(test_epd_drivers PRIVATE hal_mock)
add_test(NAME test_epd_drivers COMMAND test_epd_drivers)

add_executable(test_hal_display_rgb_oled_facade
    test_hal_display_rgb_oled_facade.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/hal_display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/hal_display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/rgb_oled_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/jh_gfx.cpp
)
target_include_directories(test_hal_display_rgb_oled_facade PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils
)
target_compile_definitions(test_hal_display_rgb_oled_facade PRIVATE
    HAL_TARGET_RP2040=1
    HAL_BOARD_PROFILE_RP_PICO=1
    HAL_ENABLE_SSD1331
    HAL_ENABLE_SSD135X
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_hal_display_rgb_oled_facade)
target_link_libraries(test_hal_display_rgb_oled_facade PRIVATE
    $<TARGET_FILE:hal_mock>
    Threads::Threads
)
add_dependencies(test_hal_display_rgb_oled_facade hal_mock)
add_test(NAME test_hal_display_rgb_oled_facade
         COMMAND test_hal_display_rgb_oled_facade)

add_executable(test_hal_display_st7567_facade
    test_hal_display_st7567_facade.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/hal_display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/hal_display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/st7567_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/jh_gfx.cpp
)
target_include_directories(test_hal_display_st7567_facade PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils
)
target_compile_definitions(test_hal_display_st7567_facade PRIVATE
    HAL_TARGET_RP2040=1
    HAL_BOARD_PROFILE_RP_PICO=1
    HAL_ENABLE_ST7567
    HAL_ENABLE_SPI
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_hal_display_st7567_facade)
target_link_libraries(test_hal_display_st7567_facade PRIVATE
    $<TARGET_FILE:hal_mock>
    Threads::Threads
)
add_dependencies(test_hal_display_st7567_facade hal_mock)
add_test(NAME test_hal_display_st7567_facade
         COMMAND test_hal_display_st7567_facade)

add_executable(test_hal_display_epd_facade
    test_hal_display_epd_facade.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/hal_display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/hal_display.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/epd_spi_transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/ssd16xx_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/uc81xx_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/jh_gfx.cpp
)
target_include_directories(test_hal_display_epd_facade PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils
)
target_compile_definitions(test_hal_display_epd_facade PRIVATE
    HAL_TARGET_RP2040=1
    HAL_BOARD_PROFILE_RP_PICO=1
    HAL_ENABLE_SSD16XX
    HAL_ENABLE_UC81XX
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_hal_display_epd_facade)
target_link_libraries(test_hal_display_epd_facade PRIVATE
    $<TARGET_FILE:hal_mock>
    Threads::Threads
)
add_dependencies(test_hal_display_epd_facade hal_mock)
add_test(NAME test_hal_display_epd_facade
         COMMAND test_hal_display_epd_facade)
add_executable(test_jh_gfx_geometry
    test_jh_gfx_geometry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers/jh_gfx.cpp
)
target_include_directories(test_jh_gfx_geometry BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/display/drivers
)
target_link_libraries(test_jh_gfx_geometry PRIVATE hal_mock)
add_test(NAME test_jh_gfx_geometry COMMAND test_jh_gfx_geometry)
add_executable(test_mcp2515_driver
    test_mcp2515_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/can/mcp2515/mcp2515_driver.cpp
)
target_link_libraries(test_mcp2515_driver PRIVATE hal_mock)
add_test(NAME test_mcp2515_driver COMMAND test_mcp2515_driver)
add_executable(test_mfrc522_driver
    test_mfrc522_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/nfc/mfrc522/mfrc522.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/nfc/mfrc522/mfrc522_spi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/nfc/mfrc522/mfrc522_i2c.cpp
)
target_compile_definitions(test_mfrc522_driver PRIVATE HAL_ENABLE_MFRC522)
target_link_libraries(test_mfrc522_driver PRIVATE hal_mock)
add_test(NAME test_mfrc522_driver COMMAND test_mfrc522_driver)
add_executable(test_pn532_driver
    test_pn532_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/nfc/pn532/pn532.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/nfc/pn532/pn532_spi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/nfc/pn532/pn532_i2c.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/nfc/pn532/pn532_uart.cpp
)
target_compile_definitions(test_pn532_driver PRIVATE HAL_ENABLE_PN532)
target_link_libraries(test_pn532_driver PRIVATE hal_mock)
add_test(NAME test_pn532_driver COMMAND test_pn532_driver)
add_executable(test_wireguard_crypto_shared
    test_wireguard_crypto_shared.cpp
)
target_include_directories(test_wireguard_crypto_shared PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/wireguard/core/crypto
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/wireguard/core
)
target_link_libraries(test_wireguard_crypto_shared PRIVATE hal_mock)
add_test(NAME test_wireguard_crypto_shared COMMAND test_wireguard_crypto_shared)
add_executable(test_wireguard_pbuf
    test_wireguard_pbuf.cpp
    fakes/lwip_fake.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_wireguard_pbuf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_wireguard_pbuf PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_wireguard_pbuf ERRORS)
add_test(NAME test_wireguard_pbuf COMMAND test_wireguard_pbuf)
add_executable(test_ntp_client
    test_ntp_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_ntp_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_ntp_client PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_ntp_client ERRORS)
add_test(NAME test_ntp_client COMMAND test_ntp_client)
add_executable(test_ota_protocol
    test_ota_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/network/ota/jh_ota_protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_ota_protocol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_ota_protocol PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_ota_protocol ERRORS)
add_test(NAME test_ota_protocol COMMAND test_ota_protocol)
add_executable(test_ff16_memdisk
    test_ff16_memdisk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/storage/filesystem/ff16/ff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_ff16_memdisk PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${JH_MANAGED_FRAMEWORK_INCLUDE_DIRS}
)
target_compile_definitions(test_ff16_memdisk PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_FAT
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_ff16_memdisk ERRORS)
add_test(NAME test_ff16_memdisk COMMAND test_ff16_memdisk)
add_hal_test(test_hal_ds18b20)
add_hal_test(test_hal_dht)
add_hal_test(test_hal_eeprom)
add_hal_test(test_hal_timer)
add_stm32_host_test(test_stm32_hal_timer)
add_stm32_host_test(test_stm32_hal_system)
add_stm32_i2c_slave_host_test(test_stm32_hal_i2c_slave)
add_stm32_gpio_host_test(test_stm32_hal_gpio)
add_executable(test_stm32_pwm_clock
    test_stm32_pwm_clock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/impl/stm32g474/hal_pwm_stm32g474.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_stm32_pwm_clock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_stm32_pwm_clock PRIVATE
    HAL_TARGET_STM32G474=1
    HAL_ENABLE_PWM_FREQ
    UNITY_INCLUDE_CONFIG_H
    HAL_ENABLE_UNITY
)
jh_enable_host_warnings(test_stm32_pwm_clock)
add_test(NAME test_stm32_pwm_clock COMMAND test_stm32_pwm_clock)
add_hal_test(test_hal_pwm)
add_hal_test(test_hal_adc)
add_hal_test(test_hal_dac)
add_hal_test(test_hal_dma_pwm_audio)
add_hal_test(test_hal_digipot)
add_hal_test(test_hal_pga2311)
add_hal_test(test_hal_pcnt)
add_hal_test(test_hal_spi)
add_hal_test(test_hal_spi_device)
add_hal_test(test_hal_lora_radio_lifecycle)
add_hal_test(test_hal_lora_radio)
add_hal_test(test_sx126x_adapter)

add_executable(test_sx127x_adapter
    test_sx127x_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/radio/sx127x/jh_sx127x_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/impl/.mock/hal_gpio.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/impl/.mock/hal_spi.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/spi/hal_spi.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/impl/.mock/hal_system.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/impl/.mock/hal_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/core/hal_assert.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/core/hal_config.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/unity.c)
target_include_directories(test_sx127x_adapter PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_sx127x_adapter PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_SX127X=1
    HAL_ENABLE_UNITY=1
    HAL_ENABLE_PWM_FREQ=1
    HAL_ENABLE_MCP2515=1
    HAL_ENABLE_RGB_LED=1
    HAL_ENABLE_WIFI=1
    UNITY_INCLUDE_CONFIG_H)
jh_enable_host_warnings(test_sx127x_adapter ERRORS)
add_test(NAME test_sx127x_adapter COMMAND test_sx127x_adapter)

add_executable(test_hal_lora_sx127x
    test_hal_lora_sx127x.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/radio/hal_lora_radio.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/radio/jh_lora_modem.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/impl/.mock/hal_lora_radio.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/impl/.mock/hal_system.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/impl/.mock/hal_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/core/jh_handle_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/core/hal_assert.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/core/hal_config.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/system/hal_board.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/unity.c)
target_include_directories(test_hal_lora_sx127x PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_hal_lora_sx127x PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_SX127X=1
    HAL_ENABLE_UNITY=1
    HAL_ENABLE_PWM_FREQ=1
    HAL_ENABLE_MCP2515=1
    HAL_ENABLE_RGB_LED=1
    HAL_ENABLE_WIFI=1
    UNITY_INCLUDE_CONFIG_H)
jh_enable_host_warnings(test_hal_lora_sx127x ERRORS)
add_test(NAME test_hal_lora_sx127x COMMAND test_hal_lora_sx127x)
add_hal_test(test_hal_i2c)
add_hal_test(test_hal_i2c_slave)
add_hal_test(test_hal_rgb_led)
add_hal_test(test_hal_serial)
add_hal_test(test_hal_sc_auth)
add_hal_test(test_hal_serial_session)
add_hal_test(test_hal_serial_session_vocabulary)
add_hal_test(test_jh_security_primitives)
add_hal_test(test_hal_status)
add_hal_test(test_hal_usb)
add_hal_test(test_flash_transaction_engine)
add_hal_test(test_hal_swserial)
add_hal_test(test_hal_onewire)
# Real bit-banging driver (JHOneWire) compiled straight in, run against mock GPIO
# so the actual reset/read/write pin sequences are covered (the public hal_onewire
# is faked under MOCK). Guards the onewire_drive_high write-before-mode ordering.
add_executable(test_hal_onewire_driver
    test_hal_onewire_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/hal/onewire/onewire_driver.cpp
)
target_link_libraries(test_hal_onewire_driver PRIVATE hal_mock)
add_test(NAME test_hal_onewire_driver COMMAND test_hal_onewire_driver)
add_hal_test(test_hal_uart)
add_hal_test(test_hal_display)
add_hal_test(test_hal_wifi)
add_hal_test(test_hal_ble)
add_hal_test(test_hal_ble_stream)
add_hal_test(test_hal_littlefs)
add_hal_test(test_hal_sdlogger)
add_hal_test(test_hal_net)
add_executable(test_hal_config_storage_flags
    test_hal_config_storage_flags.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/unity.c
)
target_include_directories(test_hal_config_storage_flags PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_definitions(test_hal_config_storage_flags PRIVATE
    HAL_TARGET_MOCK=1
    HAL_ENABLE_SDLOGGER
    HAL_ENABLE_UNITY
    UNITY_INCLUDE_CONFIG_H
)
jh_enable_host_warnings(test_hal_config_storage_flags)
add_test(NAME test_hal_config_storage_flags COMMAND test_hal_config_storage_flags)
add_hal_test(test_hal_udp)
add_hal_test(test_hal_tcp)
add_hal_test(test_hal_http_server)
add_hal_test(test_hal_http_files)
add_hal_test(test_hal_websocket)
add_hal_test(test_hal_net_console)
add_hal_test(test_hal_net_commands)
if(WIN32)
    add_platform_disabled_test(test_bsd_sockets
        "POSIX socket facade is pending a Winsock port"
        "posix_socket;windows_disabled")
else()
    add_hal_test(test_bsd_sockets)
endif()
add_hal_test(test_hal_tls)
add_hal_test(test_hal_http_client)
add_hal_test(test_bearssl_provider)

add_library(test_tls_without_bsd_compile OBJECT
    fixtures/tls_without_bsd_config_probe.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/network/tls/hal_tls.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/network/tls/BearSSL/jh_bearssl_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/network/tls/BearSSL/jh_bearssl_hal_tcp_io.cpp
    ${CMAKE_SOURCE_DIR}/src/hal/network/tls/BearSSL/jh_bearssl_provider.cpp)
target_compile_definitions(test_tls_without_bsd_compile PRIVATE
    HAL_TARGET_MOCK=1 HAL_ENABLE_TLS)
target_include_directories(test_tls_without_bsd_compile PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/BearSSL/inc)
jh_enable_host_warnings(test_tls_without_bsd_compile ERRORS)

if(NOT WIN32)
    add_library(test_bsd_without_tls_compile OBJECT
        fixtures/bsd_without_tls_config_probe.cpp
        ${CMAKE_SOURCE_DIR}/src/hal/network/adapters/bsd/hal_bsd_sockets.cpp)
    target_compile_definitions(test_bsd_without_tls_compile PRIVATE
        HAL_TARGET_MOCK=1 HAL_ENABLE_BSD_SOCKETS)
    target_include_directories(test_bsd_without_tls_compile PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    jh_enable_host_warnings(test_bsd_without_tls_compile ERRORS)
else()
    add_platform_disabled_test(test_bsd_without_tls_compile
        "POSIX socket facade is pending a Winsock port"
        "posix_socket;compile_probe;windows_disabled")
endif()

find_program(JH_OPENSSL_EXECUTABLE openssl)
if(JH_OPENSSL_EXECUTABLE AND NOT WIN32)
    add_executable(test_bearssl_native_client
        test_bearssl_native_client.cpp
        ${CMAKE_SOURCE_DIR}/src/hal/network/tls/BearSSL/jh_bearssl_bsd_io.cpp
        ${CMAKE_SOURCE_DIR}/src/hal/network/tls/BearSSL/jh_bearssl_engine.cpp
        ${CMAKE_SOURCE_DIR}/src/hal/network/tls/BearSSL/jh_bearssl_provider.cpp)
    target_compile_definitions(test_bearssl_native_client PRIVATE
        HAL_ENABLE_TLS HAL_ENABLE_BSD_SOCKETS)
    target_include_directories(test_bearssl_native_client PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party/BearSSL/inc)
    target_link_libraries(test_bearssl_native_client PRIVATE jh_bearssl_host)
    add_test(NAME test_bearssl_native_integration
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_bearssl_native_integration.sh
                $<TARGET_FILE:test_bearssl_native_client>
                ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tls_native)
elseif(WIN32)
    add_platform_disabled_test(test_bearssl_native_integration
        "native integration uses the POSIX socket transport and Bash harness"
        "bearssl;posix_socket;windows_disabled")
else()
    add_platform_disabled_test(test_bearssl_native_integration
        "OpenSSL executable is unavailable"
        "bearssl;dependency_disabled")
endif()
if(WIN32)
    add_platform_disabled_test(test_bsd_sockets_c_compile
        "POSIX socket C facade is pending a Winsock port"
        "posix_socket;windows_disabled")
else()
    add_executable(test_bsd_sockets_c_compile test_bsd_sockets_c_compile.c)
    target_link_libraries(test_bsd_sockets_c_compile PRIVATE hal_mock)
    set_target_properties(test_bsd_sockets_c_compile PROPERTIES
        LINKER_LANGUAGE CXX)
    add_test(NAME test_bsd_sockets_c_compile COMMAND test_bsd_sockets_c_compile)
endif()
add_executable(test_hal_tls_c_compile test_hal_tls_c_compile.c)
target_link_libraries(test_hal_tls_c_compile PRIVATE hal_mock)
set_target_properties(test_hal_tls_c_compile PROPERTIES LINKER_LANGUAGE CXX)
add_test(NAME test_hal_tls_c_compile COMMAND test_hal_tls_c_compile)
add_executable(test_hal_http_client_c_compile test_hal_http_client_c_compile.c)
target_link_libraries(test_hal_http_client_c_compile PRIVATE hal_mock)
set_target_properties(test_hal_http_client_c_compile PROPERTIES
    LINKER_LANGUAGE CXX)
add_test(NAME test_hal_http_client_c_compile
    COMMAND test_hal_http_client_c_compile)
add_hal_test(test_hal_wireguard)
add_hal_test(test_hal_mqtt)
add_hal_test(test_hal_network_status)
add_hal_test(test_hal_modem_at)
add_hal_test(test_hal_simcom_a76xx)
add_hal_test(test_hal_ota)
add_hal_test(test_ota_image)
add_hal_test(test_ota_swap_engine)
add_hal_test(test_hal_time)
add_hal_test(test_at24c256_provider)
add_hal_test(test_hal_rtc)
add_hal_test(test_jh_calendar)
add_hal_test(test_hal_kv)
add_hal_test(test_hal_external_adc)
add_hal_test(test_hal_gps)
add_hal_test(test_hal_system)
add_hal_test(test_hal_bits)
add_hal_test(test_hal_crypto)
add_executable(test_jpeg
    test_jpeg.cpp
)
target_link_libraries(test_jpeg PRIVATE hal_mock)
add_test(NAME test_jpeg COMMAND test_jpeg)
add_executable(test_lodepng
    test_lodepng.cpp
)
target_link_libraries(test_lodepng PRIVATE hal_mock)
add_test(NAME test_lodepng COMMAND test_lodepng)
add_hal_test(test_SmartTimers)
add_hal_test(test_hal_soft_timer)
add_hal_test(test_pidController)

if(TARGET hal_freertos_posix)
    add_hal_freertos_posix_test(test_freertos_posix_runtime)
    add_hal_freertos_posix_test(test_lora_freertos_posix)
elseif(WIN32)
    add_platform_disabled_test(test_freertos_posix_runtime
        "upstream FreeRTOS GCC/Posix host port is unavailable on Windows"
        "freertos_posix;windows_disabled")
    add_platform_disabled_test(test_lora_freertos_posix
        "upstream FreeRTOS GCC/Posix host port is unavailable on Windows"
        "freertos_posix;windows_disabled")
endif()

# test_multicoreWatchdog: compiles multicoreWatchdog.cpp directly to validate
# watchdog behavior on host.
add_executable(test_multicoreWatchdog
    test_multicoreWatchdog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils/multicoreWatchdog.cpp
)
target_link_libraries(test_multicoreWatchdog PRIVATE hal_mock)
add_test(NAME test_multicoreWatchdog COMMAND test_multicoreWatchdog)

# Exercise the parser API directly while linking its shared implementation
# through hal_mock.
add_executable(test_gps_nmea_parser
    test_gps_nmea_parser.cpp
)
target_link_libraries(test_gps_nmea_parser PRIVATE hal_mock)
add_test(NAME test_gps_nmea_parser COMMAND test_gps_nmea_parser)

# tools.cpp is linked once through hal_mock for direct utility tests and shared
# consumers such as the GPS parser.
add_executable(test_tools
    test_tools.cpp
)
target_link_libraries(test_tools PRIVATE hal_mock)
add_test(NAME test_tools COMMAND test_tools)

if(JH_ENABLE_FUZZING)
    add_hal_fuzzer(fuzz_http_server)
    add_hal_fuzzer(fuzz_websocket)
    add_hal_fuzzer(fuzz_http_multipart)
endif()
