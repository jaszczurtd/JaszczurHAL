if(NOT JH_CXX OR NOT JH_ROOT)
    message(FATAL_ERROR "JH_CXX and JH_ROOT are required")
endif()

set(_probe "${JH_ROOT}/tests/fixtures/network_config_probe.cpp")
set(_architecture_probe
    "${JH_ROOT}/tests/fixtures/network_architecture_probe.cpp")
set(_http_plaintext_probe
    "${JH_ROOT}/tests/fixtures/http_client_plaintext_config_probe.cpp")
set(_include "${JH_ROOT}/src")
include("${JH_ROOT}/tests/jh_test_artifacts.cmake")
jh_test_artifact_dir(_artifact_dir network-selection)
set(_object "${_artifact_dir}/network_config_probe.o")
set(_architecture_executable "${_artifact_dir}/network_architecture_probe")

function(check_config NAME EXPECT_SUCCESS EXPECT_TEXT)
    execute_process(
        COMMAND "${JH_CXX}" -std=c++17 "-I${_include}" ${ARGN}
                -c "${_probe}" -o "${_object}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
    )
    if(EXPECT_SUCCESS)
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR "${NAME} unexpectedly failed:\n${_stderr}")
        endif()
    else()
        if(_result EQUAL 0)
            message(FATAL_ERROR "${NAME} unexpectedly succeeded")
        endif()
        string(FIND "${_stderr}" "${EXPECT_TEXT}" _match)
        if(_match EQUAL -1)
            message(FATAL_ERROR
                "${NAME} did not emit '${EXPECT_TEXT}':\n${_stderr}")
        endif()
    endif()
endfunction()

function(check_architecture_identity NAME)
    execute_process(
        COMMAND "${JH_CXX}" -std=c++17 -Wall -Wextra -Werror
                "-I${_include}" ${ARGN}
                "${_architecture_probe}" -o "${_architecture_executable}"
        RESULT_VARIABLE _compile_result
        OUTPUT_VARIABLE _compile_stdout
        ERROR_VARIABLE _compile_stderr
    )
    if(NOT _compile_result EQUAL 0)
        message(FATAL_ERROR
            "${NAME} identity probe failed to compile:\n${_compile_stderr}")
    endif()
    execute_process(
        COMMAND "${_architecture_executable}"
        RESULT_VARIABLE _run_result
    )
    if(NOT _run_result EQUAL 0)
        message(FATAL_ERROR "${NAME} reported an unexpected network identity")
    endif()
endfunction()

function(check_http_plaintext_config)
    execute_process(
        COMMAND "${JH_CXX}" -std=c++17 "-I${_include}"
                -DHAL_TARGET_MOCK=1 -DHAL_ENABLE_HTTP_CLIENT=1
                -c "${_http_plaintext_probe}" -o "${_object}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
    )
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR
            "Plaintext HTTP configuration probe failed:\n${_stderr}")
    endif()
endfunction()

check_config(default_mock TRUE ""
    -DHAL_TARGET_MOCK=1 -DHAL_ENABLE_TCP=1)
check_http_plaintext_config()
check_config(zero_backend FALSE "requires exactly one HAL_NETWORK_BACKEND"
    -DHAL_TARGET_STM32G474=1 -DHAL_ENABLE_WIFI=1)
check_config(multiple_backends FALSE "Select exactly one HAL_NETWORK_BACKEND"
    -DHAL_TARGET_RP2040=1 -DHAL_ENABLE_WIFI=1
    -DHAL_BOARD_PROFILE_RP_PICO_W=1
    -DHAL_NETWORK_BACKEND_CYW43=1
    -DHAL_NETWORK_BACKEND_ESP_AT=1 -DHAL_ESP_AT_PROFILE_ESP8266=1)
check_config(multiple_cyw43_buses FALSE "requires exactly one HAL_CYW43_BUS"
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO=1 -DHAL_ENABLE_WIFI=1
    -DHAL_NETWORK_BACKEND_CYW43=1 -DHAL_CYW43_BUS_PICO_PIO=1
    -DHAL_CYW43_BUS_STM32_GSPI=1)
check_config(wireguard_offload FALSE "requires a host-stack L3 backend"
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO=1
    -DHAL_ENABLE_WIREGUARD=1
    -DHAL_NETWORK_BACKEND_ESP_AT=1 -DHAL_ESP_AT_PROFILE_ESP8266=1)
check_config(stm32_cyw43_gspi_lwip TRUE ""
    -DHAL_TARGET_STM32G474=1
    -DHAL_BOARD_PROFILE_STM32G474_NUCLEO_PIM730=1
    -DHAL_ENABLE_WIFI=1 -DHAL_ENABLE_TCP=1
    -DHAL_ENABLE_UDP=1 -DHAL_ENABLE_BSD_SOCKETS=1)
check_config(rp2040_network_without_board_profile FALSE
    "unknown board"
    -DHAL_TARGET_RP2040=1 -DHAL_ENABLE_WIFI=1)
check_config(rp2040_cyw43_pim730 TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_ENABLE_WIFI=1
    -DHAL_CYW43_PROFILE_PIM730=1
    -DJH_EXPECT_CYW43_GSPI_TARGET_HZ=31250000u
    -DJH_EXPECT_CYW43_PIO_CLOCK_DIV_OVERRIDE_X256=0u)
check_config(rp2040_cyw43_picow TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_ENABLE_WIFI=1
    -DHAL_CYW43_PROFILE_PICOW=1
    -DJH_EXPECT_CYW43_GSPI_TARGET_HZ=31250000u
    -DJH_EXPECT_CYW43_PIO_CLOCK_DIV_OVERRIDE_X256=0u)
check_config(rp2350_arm_cyw43_picow TRUE ""
    -DHAL_TARGET_RP2350_ARM=1 -DHAL_ENABLE_WIFI=1
    -DHAL_CYW43_PROFILE_PICOW=1
    -DJH_EXPECT_CYW43_GSPI_TARGET_HZ=31250000u
    -DJH_EXPECT_CYW43_PIO_CLOCK_DIV_OVERRIDE_X256=0u)
check_config(rp2040_cyw43_legacy_divider_override TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_ENABLE_WIFI=1
    -DHAL_CYW43_PROFILE_PIM730=1
    -DHAL_CYW43_PIO_CLOCK_DIV_INT=4u
    -DHAL_CYW43_PIO_CLOCK_DIV_FRAC8=0u
    -DJH_EXPECT_CYW43_GSPI_TARGET_HZ=31250000u
    -DJH_EXPECT_CYW43_PIO_CLOCK_DIV_OVERRIDE_X256=1024u)
check_config(rp2040_cyw43_missing_lwip FALSE
    "requires HAL_CYW43_STACK_LWIP"
    -DHAL_TARGET_RP2040=1 -DHAL_ENABLE_WIFI=1
    -DHAL_BOARD_PROFILE_RP_PICO=1
    -DHAL_NETWORK_BACKEND_CYW43=1 -DHAL_CYW43_BUS_PICO_PIO=1
    -DHAL_CYW43_MAX_TRANSACTION_BYTES=2048u)
check_config(rp2040_cyw43_on_board_without_radio TRUE ""
    -DHAL_TARGET_RP2040=1 -DHAL_ENABLE_WIFI=1
    -DHAL_BOARD_PROFILE_RP_PICO=1
    -DHAL_NETWORK_BACKEND_CYW43=1 -DHAL_CYW43_BUS_PICO_PIO=1
    -DHAL_CYW43_STACK_LWIP=1)
check_config(stm32_cyw43_on_board_without_radio FALSE
    "on STM32G474 requires a board profile with CYW43"
    -DHAL_TARGET_STM32G474=1 -DHAL_ENABLE_WIFI=1
    -DHAL_BOARD_PROFILE_STM32G474_NUCLEO_G474RE=1
    -DHAL_NETWORK_BACKEND_CYW43=1 -DHAL_CYW43_BUS_STM32_GSPI=1
    -DHAL_CYW43_STACK_LWIP=1 -DHAL_CYW43_PIN_WL_ON=30u
    -DHAL_CYW43_PIN_CHIP_SELECT=28u -DHAL_CYW43_PIN_DATA=31u
    -DHAL_CYW43_PIN_CLOCK=29u -DHAL_CYW43_MAX_TRANSACTION_BYTES=2052u)

check_architecture_identity(no_network
    -DHAL_TARGET_STM32G474=1 -DJH_EXPECT_NETWORK_NONE=1)
check_architecture_identity(mock_host_stack
    -DHAL_TARGET_MOCK=1 -DHAL_ENABLE_TCP=1 -DJH_EXPECT_NETWORK_MOCK=1)
check_architecture_identity(cyw43_host_stack
    -DHAL_TARGET_RP2040=1 -DHAL_ENABLE_TCP=1
    -DHAL_CYW43_PROFILE_PIM730=1
    -DJH_EXPECT_NETWORK_CYW43=1)
check_architecture_identity(stm32_cyw43_host_stack
    -DHAL_TARGET_STM32G474=1
    -DHAL_BOARD_PROFILE_STM32G474_NUCLEO_PIM730=1 -DHAL_ENABLE_TCP=1
    -DJH_EXPECT_NETWORK_CYW43=1)

file(READ "${JH_ROOT}/stm32_lib/CMakeLists.txt" _stm32_library_cmake)
foreach(_stm32_cyw43_contract IN ITEMS
        "jh_cyw43_driver.cmake"
        "HAL_CYW43_BUS_STM32_GSPI"
        "list(APPEND _jh_stm32_cyw43_options LWIP)"
        "jh_target_enable_cyw43_driver(JaszczurHAL \${_jh_stm32_cyw43_options})")
    string(FIND "${_stm32_library_cmake}" "${_stm32_cyw43_contract}"
        _stm32_cyw43_contract_at)
    if(_stm32_cyw43_contract_at EQUAL -1)
        message(FATAL_ERROR
            "STM32 static-library CYW43 integration is missing: "
            "${_stm32_cyw43_contract}")
    endif()
endforeach()

check_architecture_identity(esp_at_socket_offload
    -DHAL_TARGET_RP2040=1 -DHAL_BOARD_PROFILE_RP_PICO=1 -DHAL_ENABLE_TCP=1
    -DHAL_NETWORK_BACKEND_ESP_AT=1 -DHAL_ESP_AT_PROFILE_ESP8266=1
    -DJH_EXPECT_NETWORK_ESP_AT=1)
