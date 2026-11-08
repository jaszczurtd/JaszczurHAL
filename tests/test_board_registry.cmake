cmake_minimum_required(VERSION 3.22)
# The tracked board registry: its targets and boards, the facts the build
# configuration of each documented board carries, the NUCLEO-G474RE wiring,
# the component catalogue of CMake and the link library runner of every
# target. The board commands themselves are tested by the jh_boards and
# jh_features tests of tools/jh.
if(NOT DEFINED JH_ROOT OR NOT DEFINED JH_TEST_BINARY_DIR OR NOT DEFINED PYTHON)
    message(FATAL_ERROR "JH_ROOT, JH_TEST_BINARY_DIR and PYTHON are required")
endif()

set(_scratch "${JH_TEST_BINARY_DIR}/board_registry")
file(REMOVE_RECURSE "${_scratch}")
file(MAKE_DIRECTORY "${_scratch}")
include("${JH_ROOT}/cmake/jh_project_config.cmake")
jh_project_config_tool(_jh "${JH_ROOT}" "${_scratch}")

function(run_jh variable)
    execute_process(COMMAND "${_jh}" ${ARGN}
        RESULT_VARIABLE _code OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _code EQUAL 0)
        message(FATAL_ERROR "jh ${ARGN} failed (${_code}):\n${_out}\n${_err}")
    endif()
    set(${variable} "${_out}" PARENT_SCOPE)
endfunction()

# The files of the configuration of target and board: _header, _cmake and
# _resolved in the caller.
macro(board_config target board)
    run_jh(_result board config --root "${JH_ROOT}" --target ${target} --board ${board} --json)
    string(JSON _header GET "${_result}" files jh_board_config.h)
    string(JSON _cmake GET "${_result}" files jh_board_config.cmake)
    string(JSON _resolved GET "${_result}" files jh_board_resolved.json)
endmacro()

# contains, lacks and json_equals of the jh CLI checks.
include("${JH_ROOT}/tools/jh/tests/cli_helpers.cmake")

# ---- Registry ----

file(READ "${JH_ROOT}/boards/board.schema.json" _schema)
string(JSON _draft GET "${_schema}" "$schema")
if(NOT _draft STREQUAL "https://json-schema.org/draft/2020-12/schema")
    message(FATAL_ERROR "The board descriptor schema is not draft 2020-12: ${_draft}")
endif()
run_jh(_validated board validate --root "${JH_ROOT}")
if(NOT _validated STREQUAL "validated 7 targets and 12 boards\n")
    message(FATAL_ERROR "Unexpected registry size: ${_validated}")
endif()
run_jh(_listing board list --root "${JH_ROOT}")
string(REGEX MATCHALL "target [^\n]+" _targets "${_listing}")
string(REGEX MATCHALL "board [^\n]+" _boards "${_listing}")
list(TRANSFORM _targets REPLACE "^target " "")
list(TRANSFORM _boards REPLACE "^board " "")
if(NOT _targets STREQUAL "esp32;esp32s3;mock;rp2040;rp2350-arm;rp2350-riscv;stm32g474")
    message(FATAL_ERROR "Unexpected targets: ${_targets}")
endif()
if(NOT _boards STREQUAL "esp32-devkitc-v4;host-mock;nucleo-g474re;pico;pico2;pico2w;picow;rp2040-lora-lf;rp2040-plus-4mb;rp2040-zero;rp2350-plus-4mb;waveshare-esp32-s3-zero")
    message(FATAL_ERROR "Unexpected boards: ${_boards}")
endif()

# ---- RP2040 and RP2350 boards ----

# RP2040-Zero: the RP2040 facts and a WS2812 status LED, which is no GPIO LED.
board_config(rp2040 rp2040-zero)
foreach(_fact
        "#define HAL_TARGET_DESCRIPTOR_ID \"rp2040\""
        "#define HAL_TARGET_BACKEND_NAME \"rp/pico-sdk\""
        "#define HAL_TARGET_MCU_NAME \"RP2040\""
        "#define HAL_TARGET_MCU_SUBTYPE_NAME \"RP2040\""
        "#define HAL_TARGET_CPU_ARCH_NAME \"ARM Cortex-M0+\""
        "#define HAL_TARGET_CPU_CORES 2u"
        "#define HAL_TARGET_HAS_FPU 0"
        "#define HAL_TARGET_RAM_TOTAL_BYTES UINT32_C(270336)"
        "#define HAL_TARGET_RAM_USABLE_BYTES UINT32_C(262144)"
        "HAL_BOARD_STATUS_LED_KIND_ADDRESSABLE 1"
        "HAL_BOARD_STATUS_LED_PIN 16u")
    contains("${_header}" "${_fact}" "RP2040-Zero")
endforeach()
lacks("${_header}" "HAL_LED_BUILTIN" "RP2040-Zero exposes a GPIO LED")

# The 4 MB Plus boards, whose GPIO status LED provides HAL_LED_BUILTIN.
board_config(rp2040 rp2040-plus-4mb)
string(JSON _flash GET "${_resolved}" flashBytes)
if(NOT _flash EQUAL 4194304)
    message(FATAL_ERROR "RP2040-Plus flash changed: ${_flash}")
endif()
contains("${_header}" "#define HAL_LED_BUILTIN HAL_BOARD_STATUS_LED_PIN" "RP2040-Plus")
board_config(rp2350-arm rp2350-plus-4mb)
string(JSON _flash GET "${_resolved}" flashBytes)
if(NOT _flash EQUAL 4194304)
    message(FATAL_ERROR "RP2350-Plus flash changed: ${_flash}")
endif()

# The LoRa board: an SX1262 on hard-reserved pins, an XTAL without TCXO, and
# the compatible Pico SDK board definition.
board_config(rp2040 rp2040-lora-lf)
string(JSON _profile GET "${_resolved}" profileId)
string(JSON _radio GET "${_resolved}" capabilities sx1262-radio present)
string(JSON _strength GET "${_resolved}" gpio reservations lora-radio strength)
if(NOT _profile EQUAL 10 OR NOT _radio STREQUAL "ON" OR NOT _strength STREQUAL "hard")
    message(FATAL_ERROR "LoRa board profile, radio or reservation changed: ${_resolved}")
endif()
json_equals("${_resolved}" "[\"rp-pico\", \"sx126x-radio\"]" "LoRa components" components)
foreach(_fact
        "#define HAL_BOARD_PROFILE_RP2040_LORA_LF 1"
        "#define HAL_BOARD_IS_RP2040_LORA_LF 1"
        "#define HAL_BOARD_DECLARED_CAPABILITIES UINT32_C(0x00000009)"
        "#define HAL_BOARD_LORA_RADIO_PRESENT 1"
        "#define HAL_BOARD_LORA_RADIO_SPI_BUS 1u"
        "#define HAL_BOARD_LORA_RADIO_PIN_CS 13u"
        "#define HAL_BOARD_LORA_RADIO_PIN_DIO1 16u"
        "#define HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_A 17u"
        "#define HAL_BOARD_LORA_RADIO_PIN_BUSY 18u"
        "#define HAL_BOARD_LORA_RADIO_PIN_RESET 23u"
        "#define HAL_BOARD_LORA_RADIO_PIN_MISO 24u"
        "#define HAL_BOARD_LORA_RADIO_MAX_FREQUENCY_HZ UINT32_C(450000000)"
        "#define HAL_BOARD_LORA_RADIO_MAX_SPI_CLOCK_HZ UINT32_C(17999999)"
        "#define HAL_BOARD_LORA_RADIO_DEFAULT_SPI_CLOCK_HZ UINT32_C(8000000)"
        "#define HAL_BOARD_LORA_RADIO_REGULATOR_IS_DCDC 1"
        "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_DIO2_SINGLE_GPIO 1"
        "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_TX_LEVEL_A 0"
        "#define HAL_BOARD_LORA_RADIO_TCXO_CONTROL_IS_NONE 1"
        "#define HAL_BOARD_LORA_RADIO_TCXO_CONTROL_IS_DIO3 0"
        "#define HAL_BOARD_LORA_RADIO_TCXO_VOLTAGE_IS_1V7 0")
    contains("${_header}" "${_fact}" "LoRa board")
endforeach()
lacks("${_header}" "HAL_BOARD_LORA_RADIO_TCXO_VOLTAGE_NAME" "XTAL LoRa board")
lacks("${_header}" "HAL_BOARD_LORA_RADIO_TCXO_STARTUP_US" "XTAL LoRa board")
contains("${_cmake}" "set(PICO_BOARD \"pico\")" "LoRa board")

# Pico W: the components select the CYW43 transport, whose definitions reach
# both the build and the installed header.
board_config(rp2040 picow)
contains("${_header}" "#define HAL_LED_BUILTIN HAL_BOARD_STATUS_LED_PIN" "Pico W")
json_equals("${_resolved}" "[\"btstack-host\", \"cyw43-lwip\", \"cyw43-pico-pio\", \"rp-pico\"]"
    "Pico W components" components)
string(JSON _cyw43 GET "${_resolved}" capabilities cyw43 present)
string(JSON _bluetooth GET "${_resolved}" capabilities bluetooth-controller present)
if(NOT _cyw43 STREQUAL "ON" OR NOT _bluetooth STREQUAL "ON")
    message(FATAL_ERROR "Pico W lost its CYW43 or Bluetooth controller capability")
endif()
set(_picow_definitions HAL_NETWORK_BACKEND_CYW43 HAL_CYW43_BUS_PICO_PIO HAL_CYW43_STACK_LWIP
    HAL_CYW43_MAX_TRANSACTION_BYTES=2048u)
json_equals("${_resolved}" "[\"HAL_NETWORK_BACKEND_CYW43\", \"HAL_CYW43_BUS_PICO_PIO\", \"HAL_CYW43_STACK_LWIP\", \"HAL_CYW43_MAX_TRANSACTION_BYTES=2048u\"]"
    "Pico W compile definitions" boardCompileDefinitions)
foreach(_definition IN LISTS _picow_definitions)
    contains("${_cmake}" "${_definition}" "Pico W CMake")
    if(_definition MATCHES "^([^=]+)=(.*)$")
        contains("${_header}" "#define ${CMAKE_MATCH_1} ${CMAKE_MATCH_2}" "Pico W header")
    else()
        contains("${_header}" "#define ${_definition} 1" "Pico W header")
    endif()
endforeach()

# ---- ESP32 boards ----

# Waveshare ESP32-S3-Zero: ESP-IDF selects the target, FreeRTOS is the
# target's own requirement, and the board has quad PSRAM.
board_config(esp32s3 waveshare-esp32-s3-zero)
string(JSON _idf GET "${_resolved}" idfTarget)
if(NOT _idf STREQUAL "esp32s3")
    message(FATAL_ERROR "ESP-IDF target mapping changed: ${_idf}")
endif()
json_equals("${_resolved}" "[\"esp-idf\"]" "ESP-IDF runtime component" components)
json_equals("${_resolved}" "[]" "A target feature became a project request" requestedFeatures)
json_equals("${_resolved}" "[\"HAL_ENABLE_FREERTOS\"]" "ESP32-S3 FreeRTOS" resolvedFeatures)
json_equals("${_resolved}" "[\"target:esp32s3:requiredFeatures[0]\"]"
    "Target-required FreeRTOS provenance" featureProvenance HAL_ENABLE_FREERTOS)
json_equals("${_resolved}" "{\"interface\": \"quad\", \"sizeBytes\": 2097152}"
    "Waveshare PSRAM" psram)
foreach(_fact
        "#define HAL_TARGET_DESCRIPTOR_ID \"esp32s3\""
        "#define HAL_TARGET_BACKEND_NAME \"esp-idf\""
        "#define HAL_TARGET_CPU_CORES 2u"
        "#define HAL_TARGET_HAS_FPU 1"
        "#define HAL_BOARD_EXPECTED_FLASH_BYTES UINT32_C(4194304)"
        "#define HAL_BOARD_EXPECTED_FLASH_MIB 4"
        "#define HAL_BOARD_HAS_PSRAM 1"
        "#define HAL_BOARD_PSRAM_BYTES UINT32_C(2097152)"
        "#define HAL_BOARD_PSRAM_INTERFACE_QUAD 1"
        "#define HAL_BOARD_HAS_NATIVE_WIFI 1"
        "#define HAL_BOARD_STATUS_LED_PIN 21u"
        "#define HAL_TARGET_GPIO_VALID_MASK UINT64_C(0x0001fffffc3fffff)"
        "#define HAL_TARGET_GPIO_INPUT_ONLY_MASK UINT64_C(0x0000400000000000)"
        "#define HAL_TARGET_GPIO_ADC_MASK UINT64_C(0x00000000001ffffe)"
        "#define HAL_BOARD_GPIO_EXPOSED_MASK UINT64_C(0x000027c00007fffe)"
        "#define HAL_BOARD_GPIO_HARD_RESERVED_MASK UINT64_C(0x0000003e00180000)"
        "#define HAL_BOARD_GPIO_SOFT_RESERVED_MASK UINT64_C(0x0000000000200001)")
    contains("${_header}" "${_fact}" "ESP32-S3 configuration")
endforeach()
file(READ "${JH_ROOT}/boards/profiles/waveshare-esp32-s3-zero.json" _waveshare)
json_equals("${_waveshare}" [=[{"transport": "usb-serial-jtag",
    "usb": {"vid": "0x303A", "pid": "0x1001"},
    "reset": "usb-serial-jtag-control-lines", "boot": "usb-serial-jtag-control-lines"}]=]
    "Waveshare programming identity and wiring" programming)
# The ESP32-S3 Phase 3 feature boundary.
file(READ "${JH_ROOT}/boards/targets/esp32s3.json" _esp32s3)
json_equals("${_esp32s3}" [=[["HAL_ENABLE_A7670", "HAL_ENABLE_ADC_SCAN", "HAL_ENABLE_ADP5360",
    "HAL_ENABLE_APP_TASK1", "HAL_ENABLE_BH1750", "HAL_ENABLE_BLE", "HAL_ENABLE_BSD_SOCKETS",
    "HAL_ENABLE_CAN", "HAL_ENABLE_CELLULAR_MODEM", "HAL_ENABLE_CJSON",
    "HAL_ENABLE_COMMAND_ROUTER", "HAL_ENABLE_CRC", "HAL_ENABLE_CRYPTO", "HAL_ENABLE_DACLESS",
    "HAL_ENABLE_DHT", "HAL_ENABLE_DIGIPOT", "HAL_ENABLE_DISPLAY", "HAL_ENABLE_DMA_PWM_AUDIO",
    "HAL_ENABLE_DS18B20", "HAL_ENABLE_DS3231", "HAL_ENABLE_EXTERNAL_ADC", "HAL_ENABLE_FAT",
    "HAL_ENABLE_FREERTOS", "HAL_ENABLE_GC9A01", "HAL_ENABLE_GPS", "HAL_ENABLE_HC595",
    "HAL_ENABLE_HD44780", "HAL_ENABLE_HTTP_CLIENT", "HAL_ENABLE_HTTP_FILES",
    "HAL_ENABLE_HTTP_SERVER", "HAL_ENABLE_I2C", "HAL_ENABLE_I2C_10BIT", "HAL_ENABLE_I2C_SLAVE",
    "HAL_ENABLE_I2C_SLAVE_SNAPSHOT", "HAL_ENABLE_ILI9341", "HAL_ENABLE_IRSMALL_DECODER",
    "HAL_ENABLE_JPEG", "HAL_ENABLE_JPEG_AS_BASE64", "HAL_ENABLE_MAX5395", "HAL_ENABLE_MAX6675",
    "HAL_ENABLE_MCP23017", "HAL_ENABLE_MCP2515", "HAL_ENABLE_MCP251XFD", "HAL_ENABLE_MCP3221",
    "HAL_ENABLE_MCP401X", "HAL_ENABLE_MCP4725", "HAL_ENABLE_MCP9600", "HAL_ENABLE_MFRC522",
    "HAL_ENABLE_MQTT", "HAL_ENABLE_NETWORK_CORE", "HAL_ENABLE_NET_COMMANDS",
    "HAL_ENABLE_NET_CONSOLE", "HAL_ENABLE_NOTIFY", "HAL_ENABLE_NOTIFY_TELEGRAM",
    "HAL_ENABLE_ONEWIRE", "HAL_ENABLE_OTA", "HAL_ENABLE_PCA9654E", "HAL_ENABLE_PCF8563",
    "HAL_ENABLE_PCF8574", "HAL_ENABLE_PCNT", "HAL_ENABLE_PGA2311", "HAL_ENABLE_PN532",
    "HAL_ENABLE_PNG", "HAL_ENABLE_PNG_AS_BASE64", "HAL_ENABLE_PULSE_CAPTURE",
    "HAL_ENABLE_PWM_FREQ", "HAL_ENABLE_RGB_LED", "HAL_ENABLE_RTC", "HAL_ENABLE_SERIAL_COMMANDS",
    "HAL_ENABLE_SPI", "HAL_ENABLE_SSD1306", "HAL_ENABLE_SSD1331", "HAL_ENABLE_SSD135X",
    "HAL_ENABLE_SSD16XX", "HAL_ENABLE_ST7567", "HAL_ENABLE_ST7735", "HAL_ENABLE_ST7789",
    "HAL_ENABLE_ST7796S", "HAL_ENABLE_STACK_GUARD", "HAL_ENABLE_STMPE610", "HAL_ENABLE_SWSERIAL",
    "HAL_ENABLE_TCP", "HAL_ENABLE_TFT", "HAL_ENABLE_THERMOCOUPLE", "HAL_ENABLE_TIME",
    "HAL_ENABLE_TLS", "HAL_ENABLE_TSC2007", "HAL_ENABLE_UART", "HAL_ENABLE_UC81XX",
    "HAL_ENABLE_UDP", "HAL_ENABLE_WEBSOCKET", "HAL_ENABLE_WIFI", "HAL_ENABLE_WIREGUARD"]]=]
    "ESP32-S3 feature boundary" supportedFeatures)

# ESP32-DevKitC V4: the original ESP32, its Bluetooth controllers and its
# feature boundary.
board_config(esp32 esp32-devkitc-v4)
string(JSON _idf GET "${_resolved}" idfTarget)
if(NOT _idf STREQUAL "esp32")
    message(FATAL_ERROR "Original ESP32 target mapping changed: ${_idf}")
endif()
json_equals("${_resolved}" "[\"HAL_ENABLE_FREERTOS\"]" "Original ESP32 FreeRTOS" resolvedFeatures)
foreach(_fact
        "#define HAL_TARGET_DESCRIPTOR_ID \"esp32\""
        "#define HAL_BOARD_HAS_BLUETOOTH_CLASSIC_CONTROLLER 1"
        "#define HAL_BOARD_HAS_BLUETOOTH_LE_CONTROLLER 1")
    contains("${_header}" "${_fact}" "Original ESP32")
endforeach()
file(READ "${JH_ROOT}/boards/targets/esp32.json" _esp32)
json_equals("${_esp32}" [=[["HAL_ENABLE_BLUETOOTH_CLASSIC", "HAL_ENABLE_BLUETOOTH_HID_HOST",
    "HAL_ENABLE_BLUETOOTH_GAMEPAD", "HAL_ENABLE_CRC", "HAL_ENABLE_FREERTOS"]]=]
    "Original ESP32 feature boundary" supportedFeatures)

# ---- NUCLEO-G474RE ----

# The STM32G474RE facts, the PA5 status LED and a GPIO set beyond 64 pins in
# two mask words, without a truncated single mask.
board_config(stm32g474 nucleo-g474re)
foreach(_fact
        "#define HAL_BOARD_STATUS_LED_PIN 5u"
        "#define HAL_TARGET_DESCRIPTOR_ID \"stm32g474\""
        "#define HAL_TARGET_BACKEND_NAME \"stm32g474/bare-metal\""
        "#define HAL_TARGET_MCU_NAME \"STM32G474RE\""
        "#define HAL_TARGET_CPU_ARCH_NAME \"ARM Cortex-M4F\""
        "#define HAL_TARGET_CPU_CORES 1u"
        "#define HAL_TARGET_HAS_FPU 1"
        "#define HAL_TARGET_RAM_TOTAL_BYTES UINT32_C(131072)"
        "#define HAL_TARGET_RAM_USABLE_BYTES UINT32_C(98304)"
        "#define HAL_BOARD_GPIO_HARD_RESERVED_MASK_WORDS 2u"
        # PA13/PA14 (13, 14) and PC14/PC15 (46, 47) in the first word.
        "#define HAL_BOARD_GPIO_HARD_RESERVED_MASK_0 UINT64_C(0x0000c00000006000)"
        # PF0/PF1 (80, 81) and PG10 (106) in the second word.
        "#define HAL_BOARD_GPIO_HARD_RESERVED_MASK_1 UINT64_C(0x0000040000030000)"
        "#define HAL_BOARD_GPIO_EXPOSED_MASK_1 UINT64_C(0x0000000000000000)"
        "#define HAL_TARGET_GPIO_VALID_MASK_WORDS 2u")
    contains("${_header}" "${_fact}" "NUCLEO-G474RE")
endforeach()
lacks("${_header}" "#define HAL_BOARD_GPIO_HARD_RESERVED_MASK " "NUCLEO-G474RE GPIO words")
string(JSON _hse GET "${_resolved}" clockSources hse frequencyHz)
if(NOT _hse EQUAL 24000000)
    message(FATAL_ERROR "The resolved configuration lost the clock sources: ${_hse}")
endif()

# The board as delivered (ST UM2505): the LQFP64 pins on the connectors,
# without SWD, the HSE crystal X3 (PF0/PF1), the LSE crystal X2 (PC14/PC15)
# and NRST (PG10), each hard-reserved by its owner.
file(READ "${JH_ROOT}/boards/profiles/nucleo-g474re.json" _nucleo)
set(_exposed "")
foreach(_port_range "PA 0 12" "PB 0 15" "PC 0 13")
    separate_arguments(_port_range)
    list(GET _port_range 0 _port)
    list(GET _port_range 1 _first)
    list(GET _port_range 2 _last)
    foreach(_index RANGE ${_first} ${_last})
        list(APPEND _exposed "\"${_port}${_index}\"")
    endforeach()
    if(_port STREQUAL "PA")
        list(APPEND _exposed "\"PA15\"")
    endif()
endforeach()
list(APPEND _exposed "\"PD2\"")
list(JOIN _exposed ", " _exposed)
json_equals("${_nucleo}" "{\"values\": [${_exposed}]}" "NUCLEO-G474RE exposed pins" gpio exposedPins)
string(JSON _reservations GET "${_nucleo}" gpio reservations)
string(JSON _reservation_count LENGTH "${_reservations}")
math(EXPR _reservation_last "${_reservation_count} - 1")
set(_owners "")
foreach(_index RANGE ${_reservation_last})
    string(JSON _name MEMBER "${_reservations}" ${_index})
    string(JSON _strength GET "${_reservations}" ${_name} strength)
    if(NOT _strength STREQUAL "hard")
        continue()
    endif()
    string(JSON _owner GET "${_reservations}" ${_name} owner)
    string(JSON _pin_count LENGTH "${_reservations}" ${_name} pins)
    math(EXPR _pin_last "${_pin_count} - 1")
    foreach(_pin RANGE ${_pin_last})
        string(JSON _id GET "${_reservations}" ${_name} pins ${_pin} id)
        list(APPEND _owners "${_id}=${_owner}")
    endforeach()
endforeach()
foreach(_expected PG10=board.reset PF0=clock.hse PF1=clock.hse PC14=clock.lse PC15=clock.lse)
    if(NOT _expected IN_LIST _owners)
        message(FATAL_ERROR "NUCLEO-G474RE: no hard reservation ${_expected}: ${_owners}")
    endif()
endforeach()
string(JSON _sources GET "${_nucleo}" clockSources)
string(JSON _source_count LENGTH "${_sources}")
set(_kinds "")
math(EXPR _source_last "${_source_count} - 1")
foreach(_index RANGE ${_source_last})
    string(JSON _name MEMBER "${_sources}" ${_index})
    string(JSON _kind GET "${_sources}" ${_name} kind)
    string(JSON _frequency GET "${_sources}" ${_name} frequencyHz)
    list(APPEND _kinds "${_name}=${_kind}/${_frequency}")
endforeach()
if(NOT _kinds STREQUAL "hse=crystal/24000000;lse=crystal/32768")
    message(FATAL_ERROR "NUCLEO-G474RE: the crystals X3 and X2 are not described: ${_kinds}")
endif()

# ---- Component catalogue ----

# The catalogue CMake reads lists the components of jh board list, each with
# its providers and slot.
run_jh(_registry board list --root "${JH_ROOT}" --json)
include("${JH_ROOT}/cmake/generated/jh_board_components_registry.cmake")
string(JSON _component_count LENGTH "${_registry}" components)
math(EXPR _component_last "${_component_count} - 1")
set(_component_ids "")
foreach(_index RANGE ${_component_last})
    string(JSON _id GET "${_registry}" components ${_index} id)
    string(JSON _slot GET "${_registry}" components ${_index} slot)
    list(APPEND _component_ids "${_id}")
    string(REPLACE "-" "_" _key "${_id}")
    if(NOT JH_BOARD_COMPONENT_${_key}_SLOT STREQUAL _slot)
        message(FATAL_ERROR "CMake catalogue lost the slot of ${_id}")
    endif()
    string(JSON _provider_count LENGTH "${_registry}" components ${_index} providers)
    math(EXPR _provider_last "${_provider_count} - 1")
    foreach(_provider_index RANGE ${_provider_last})
        string(JSON _provider GET "${_registry}" components ${_index} providers ${_provider_index})
        if(NOT _provider IN_LIST JH_BOARD_COMPONENT_${_key}_PROVIDERS)
            message(FATAL_ERROR "CMake catalogue lost provider ${_provider} of ${_id}")
        endif()
    endforeach()
endforeach()
list(SORT _component_ids)
set(_cmake_ids ${JH_BOARD_COMPONENT_IDS})
list(SORT _cmake_ids)
if(NOT _cmake_ids STREQUAL _component_ids)
    message(FATAL_ERROR "CMake catalogue ${_cmake_ids} differs from jh ${_component_ids}")
endif()

# ---- Link library runners ----

# Every target outside the host has the runner of its build provider, and the
# facts scripts/board_registry.py gives the runners are those of the registry.
set(_runner_pico-sdk "build_rp_pico_lib.sh;rp_pico_lib")
set(_runner_jh-stm32-baremetal "build_stm32_lib.sh;stm32_lib")
set(_runner_esp-idf "build_esp32_lib.sh;esp32_lib")
string(JSON _target_count LENGTH "${_registry}" targets)
math(EXPR _target_last "${_target_count} - 1")
function(target_facts variable target expected_code)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env "JH_EXECUTABLE=${_jh}"
        "JH_EXECUTABLE_ROOT=${JH_ROOT}" "${PYTHON}" "${JH_ROOT}/scripts/board_registry.py"
        --repo-root "${JH_ROOT}" target-facts ${target}
        RESULT_VARIABLE _code OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _code EQUAL expected_code)
        message(FATAL_ERROR "target-facts ${target}: expected exit ${expected_code}, got ${_code}\n${_out}\n${_err}")
    endif()
    set(${variable} "${_out}${_err}" PARENT_SCOPE)
endfunction()
foreach(_index RANGE ${_target_last})
    string(JSON _id GET "${_registry}" targets ${_index} id)
    string(JSON _provider GET "${_registry}" targets ${_index} descriptor build provider)
    string(JSON _default GET "${_registry}" targets ${_index} descriptor defaultBoard)
    if(_provider STREQUAL "host")
        continue()
    endif()
    if(NOT DEFINED _runner_${_provider})
        message(FATAL_ERROR "${_id}: provider ${_provider} has no linkable-library runner")
    endif()
    list(GET _runner_${_provider} 0 _script)
    list(GET _runner_${_provider} 1 _directory)
    if(NOT EXISTS "${JH_ROOT}/scripts/${_script}")
        message(FATAL_ERROR "${_id}: runner script is missing: scripts/${_script}")
    endif()
    if(NOT IS_DIRECTORY "${JH_ROOT}/link_libraries/${_directory}")
        message(FATAL_ERROR "${_id}: runner directory is missing: link_libraries/${_directory}")
    endif()
    target_facts(_facts ${_id} 0)
    if(NOT _facts MATCHES "(^|\n)provider=${_provider}\n" OR
       NOT _facts MATCHES "(^|\n)defaultBoard=${_default}\n")
        message(FATAL_ERROR "${_id}: target-facts drifted from the registry:\n${_facts}")
    endif()
endforeach()
target_facts(_facts no-such-target 2)
contains("${_facts}" "unknown target 'no-such-target'" "target-facts of an unknown target")
file(REMOVE_RECURSE "${_scratch}")
