cmake_minimum_required(VERSION 3.22)
# The board commands of jh beyond the cases of check_features.cmake: the build
# configuration of equivalent feature requests, its feature hash, digest and
# link contract, board CAN channels, the descriptors jh board validate rejects
# on edited copies of boards/, the bus devices of a board and, with a GCC or
# Clang compiler, the fallback header against each generated configuration
# and the link contract of a board library. The facts of the tracked registry
# are checked by tests/test_board_registry.cmake.
if(NOT EXISTS "${JH}" OR NOT IS_DIRECTORY "${JH_HAL_SOURCE}/boards")
  message(FATAL_ERROR "jh_boards needs JH and JH_HAL_SOURCE")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/cli_helpers.cmake")
file(REMOVE_RECURSE "${JH_WORK}")
file(MAKE_DIRECTORY "${JH_WORK}/cwd")
set(fixtures "${JH_WORK}/fixtures")
# Configurations of the tracked registry go below this output root: the build
# tree need not lie below a .build directory.
set(generated "${JH_WORK}/.build")

# edit_json(<root> <file> [PREFIX <path>...] OPERATIONS <operation>...):
# apply to file below root the string(JSON) operations, each
# <SET|REMOVE> <path...> [value], separated by "|", with PREFIX before each
# path. string(JSON SET) writes the members of every object in name order; no
# case depends on that order.
function(edit_json root file)
  cmake_parse_arguments(PARSE_ARGV 2 edit "" "" "PREFIX;OPERATIONS")
  set(path "${root}/${file}")
  file(READ "${path}" text)
  set(operation "")
  foreach(item IN LISTS edit_OPERATIONS ITEMS "|")
    if(item STREQUAL "|")
      if(operation)
        list(POP_FRONT operation verb)
        string(JSON text ${verb} "${text}" ${edit_PREFIX} ${operation})
      endif()
      set(operation "")
    else()
      list(APPEND operation "${item}")
    endif()
  endforeach()
  file(WRITE "${path}" "${text}")
endfunction()

# A fresh copy of boards/ at fixtures/case, with the operations of ARGN on
# its file relative.
function(fixture case relative)
  file(REMOVE_RECURSE "${fixtures}/${case}")
  file(COPY "${JH_HAL_SOURCE}/boards/" DESTINATION "${fixtures}/${case}")
  edit_json("${fixtures}/${case}" "${relative}" OPERATIONS ${ARGN})
endfunction()

# jh board validate must accept, or reject with expected in its message, the
# copy at fixtures/case.
function(validates case)
  invoke(0 board validate --root "${JH_HAL_SOURCE}" --boards-root "${fixtures}/${case}")
endfunction()
function(rejected case expected)
  invoke(2 board validate --root "${JH_HAL_SOURCE}" --boards-root "${fixtures}/${case}")
  string(FIND "${stderr}" "${expected}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "${case}: expected a rejection with '${expected}', got: ${stderr}")
  endif()
endfunction()

# The configuration of target and board of the copy at fixtures/case, written
# to the .build directory next to the copy, which the output root defaults to.
function(configure_fixture case target board)
  invoke(0 board config --root "${JH_HAL_SOURCE}" --boards-root "${fixtures}/${case}"
    --target "${target}" --board "${board}" --output-dir "${fixtures}/.build/${case}" ${ARGN})
endfunction()

# The configuration of target and board of the tracked registry at
# generated/name.
function(configure name target board)
  invoke(0 board config --root "${JH_HAL_SOURCE}" --target "${target}" --board "${board}"
    --output-root "${generated}" --output-dir "${generated}/${name}" ${ARGN})
endfunction()

# The JSON result of board config with ARGN, in variable.
function(config_json variable)
  invoke(0 board config --root "${JH_HAL_SOURCE}" --json ${ARGN})
  set(${variable} "${stdout}" PARENT_SCOPE)
endfunction()

# ---- Equivalent feature requests ----

# The same request through --define, --feature and their spellings gives the
# same files; a jh_board_registry.h an older generator left goes.
file(MAKE_DIRECTORY "${generated}/first")
file(WRITE "${generated}/first/jh_board_registry.h" "stale per-build registry\n")
configure(first rp2040 rp2040-zero --define HAL_ENABLE_WIFI
  --feature HAL_ENABLE_WIFI --feature HAL_ENABLE_FREERTOS=1)
configure(second rp2040 rp2040-zero --define=-DHAL_ENABLE_WIFI=1
  --feature HAL_ENABLE_FREERTOS --feature HAL_ENABLE_WIFI=1)
foreach(name jh_board_config.cmake jh_board_config.h jh_board_resolved.json
             jh_link_contract.h jh_link_contract_definition.c jh_link_contract_reference.c)
  file(SHA256 "${generated}/first/${name}" first_hash)
  file(SHA256 "${generated}/second/${name}" second_hash)
  if(NOT first_hash STREQUAL second_hash)
    message(FATAL_ERROR "${name} differs for equivalent feature sets")
  endif()
endforeach()
if(EXISTS "${generated}/first/jh_board_registry.h" OR EXISTS "${generated}/second/jh_board_registry.h")
  message(FATAL_ERROR "The configuration of one build kept a second board registry")
endif()

# The reference keeps the contract through a constructor root, not through
# the unsupported retain attribute.
file(READ "${generated}/first/jh_link_contract_reference.c" reference)
contains("${reference}" "__attribute__((constructor, used))" "Link reference")
lacks("${reference}" "retain" "Link reference")

# The requested and resolved features, their digest, the feature hash of the
# profile ID and closure, and the contract symbol named after it.
file(READ "${generated}/first/jh_board_resolved.json" resolved)
json_equals("${resolved}" "[\"HAL_ENABLE_FREERTOS\", \"HAL_ENABLE_WIFI\"]"
  "Requested features" requestedFeatures)
set(closure "[\"HAL_ENABLE_FREERTOS\", \"HAL_ENABLE_NETWORK_CORE\", \"HAL_ENABLE_WIFI\"]")
json_equals("${resolved}" "${closure}" "Resolved features" resolvedFeatures)
json_equals("${resolved}" "${closure}" "The features alias" features)
string(SHA256 resolved_digest "HAL_ENABLE_FREERTOS\nHAL_ENABLE_NETWORK_CORE\nHAL_ENABLE_WIFI")
string(SHA256 feature_hash
  "hal.profileId=8\nHAL_ENABLE_FREERTOS=1\nHAL_ENABLE_NETWORK_CORE=1\nHAL_ENABLE_WIFI=1")
string(SUBSTRING "${feature_hash}" 0 12 feature_hash)
string(JSON digest GET "${resolved}" resolvedFeaturesDigest)
string(JSON hash GET "${resolved}" featureHash)
string(JSON contract_symbol GET "${resolved}" contractSymbol)
if(NOT digest STREQUAL resolved_digest OR NOT hash STREQUAL feature_hash OR
   NOT contract_symbol STREQUAL "jh_board_contract_rp2040_rp2040_zero_${feature_hash}")
  message(FATAL_ERROR "Digest, feature hash or contract symbol changed: ${resolved}")
endif()
file(READ "${generated}/first/jh_board_config.cmake" config_cmake)
contains("${config_cmake}"
  "set(JH_BOARD_REQUESTED_FEATURES \"HAL_ENABLE_FREERTOS;HAL_ENABLE_WIFI\")" "Board CMake")
contains("${config_cmake}"
  "set(JH_BOARD_RESOLVED_FEATURES \"HAL_ENABLE_FREERTOS;HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_WIFI\")"
  "Board CMake")
contains("${config_cmake}"
  "set(JH_BOARD_RESOLVED_FEATURES_DIGEST \"${resolved_digest}\")" "Board CMake")
contains("${config_cmake}"
  "set(JH_BOARD_FEATURES \"HAL_ENABLE_FREERTOS;HAL_ENABLE_NETWORK_CORE;HAL_ENABLE_WIFI\")"
  "Board CMake")

# A request its closure already holds changes neither the closure, nor the
# feature hash, nor the link contract; the requests themselves stay as given.
config_json(minimal --target rp2040 --board rp2040-zero --feature HAL_ENABLE_MQTT)
config_json(redundant --target rp2040 --board rp2040-zero --feature HAL_ENABLE_MQTT
  --feature HAL_ENABLE_TCP --feature HAL_ENABLE_WIFI)
string(JSON minimal GET "${minimal}" files jh_board_resolved.json)
string(JSON redundant GET "${redundant}" files jh_board_resolved.json)
json_equals("${minimal}" "[\"HAL_ENABLE_MQTT\"]" "Minimal MQTT request" requestedFeatures)
json_equals("${redundant}" "[\"HAL_ENABLE_MQTT\", \"HAL_ENABLE_TCP\", \"HAL_ENABLE_WIFI\"]"
  "Redundant MQTT requests" requestedFeatures)
foreach(document minimal redundant)
  string(JSON alias GET "${${document}}" features)
  json_equals("${${document}}" "${alias}" "The ${document} features alias" resolvedFeatures)
endforeach()
string(JSON minimal_closure GET "${minimal}" resolvedFeatures)
json_equals("${redundant}" "${minimal_closure}" "Equivalent MQTT closures" resolvedFeatures)
foreach(field featureHash contractSymbol)
  string(JSON minimal_value GET "${minimal}" ${field})
  string(JSON redundant_value GET "${redundant}" ${field})
  if(NOT minimal_value STREQUAL redundant_value)
    message(FATAL_ERROR "Equivalent MQTT closures gave another ${field}")
  endif()
endforeach()

# ---- Board CAN channels ----

# A board with its own transceivers keeps its CAN channels: the CN5-CN7 wiring
# of the CAN-FD HAT on a NUCLEO-G474RE stands in for one, with the HSE crystal
# component of its clock. The channels take the pins of the default SPI1 chip
# select and of FDCAN1.
set(can_channels "[")
set(can_pins "")
foreach(channel "1 PA11 PA12 PB11" "2 PB12 PB13 PC7" "3 PA8 PB4 PB6")
  separate_arguments(channel)
  list(GET channel 0 instance)
  list(GET channel 1 rx)
  list(GET channel 2 tx)
  list(GET channel 3 standby)
  if(NOT instance EQUAL 1)
    string(APPEND can_channels ", ")
  endif()
  string(APPEND can_channels "{\"controller\": \"stm32g474-fdcan\", \"instance\": ${instance}, "
    "\"rx\": {\"domain\": \"soc-gpio\", \"id\": \"${rx}\"}, "
    "\"tx\": {\"domain\": \"soc-gpio\", \"id\": \"${tx}\"}, "
    "\"standby\": {\"domain\": \"soc-gpio\", \"id\": \"${standby}\"}, "
    "\"standbyActiveHigh\": true, \"transceiver\": \"mcp2562fd\", \"maxBitrateHz\": 5000000}")
  foreach(pin ${rx} ${tx} ${standby})
    list(APPEND can_pins "{\"domain\": \"soc-gpio\", \"id\": \"${pin}\"}")
  endforeach()
endforeach()
string(APPEND can_channels "]")
list(JOIN can_pins ", " can_pins)
set(can_board_edits
  SET can "{\"channels\": ${can_channels}}" |
  SET gpio reservations can-channels "{\"pins\": [${can_pins}], \"owner\": \"board.can\", \"strength\": \"hard\", \"reason\": \"FDCAN1-3 RX/TX and the transceiver standby inputs.\"}" |
  SET peripherals defaults "{}" |
  SET components stm32g474-hse-24mhz "{\"mode\": \"board-owned\"}")
fixture(can-board profiles/nucleo-g474re.json ${can_board_edits})
configure_fixture(can-board stm32g474 nucleo-g474re)
file(READ "${fixtures}/.build/can-board/jh_board_resolved.json" can_resolved)
file(READ "${fixtures}/.build/can-board/jh_board_config.h" can_header)
json_equals("${can_resolved}" "[\"stm32g474\", \"stm32g474-hse-24mhz\"]"
  "Board-level CAN components" components)
string(JSON can_definitions GET "${can_resolved}" boardCompileDefinitions)
contains("${can_definitions}" "\"HAL_STM32G474_CLOCK_HSE_160MHZ\""
  "The HSE crystal component must select the 160 MHz clock tree")
contains("${can_header}" "#define HAL_STM32G474_CLOCK_HSE_160MHZ 1" "Board CAN header")
contains("${can_header}" "#define HAL_BOARD_CAN_CHANNEL_COUNT 3\n" "Board CAN header")
contains("${can_header}" "#define HAL_BOARD_CAN_CHANNELS(X) X(STM32G474_FDCAN, 1u, 11u, 12u, 27u, 1, UINT32_C(5000000)) X(STM32G474_FDCAN, 2u, 28u, 29u, 39u, 1, UINT32_C(5000000)) X(STM32G474_FDCAN, 3u, 8u, 20u, 22u, 1, UINT32_C(5000000))"
  "Board CAN header")
# A board without CAN channels, and only the HSE crystal component selects the
# 160 MHz tree.
config_json(nucleo --target stm32g474 --board nucleo-g474re)
string(JSON nucleo_header GET "${nucleo}" files jh_board_config.h)
contains("${nucleo_header}" "#define HAL_BOARD_CAN_CHANNEL_COUNT 0\n" "Board without CAN")
contains("${nucleo_header}" "#define HAL_BOARD_CAN_CHANNELS(X)\n" "Board without CAN")
lacks("${nucleo_header}" "HAL_STM32G474_CLOCK_HSE_160MHZ" "Board without the HSE crystal")

# Channel faults jh board validate rejects, each on the CAN board above.
function(can_case case expected)
  fixture(${case} profiles/nucleo-g474re.json ${can_board_edits} | ${ARGN})
  rejected(${case} "${expected}")
endfunction()
can_case(can-duplicate-instance "an instance not already wired by channel 0"
  SET can channels 1 instance 1)
can_case(can-instance-range "an instance in [1, 3]" SET can channels 2 instance 4)
can_case(can-unknown-controller "one of ['stm32g474-fdcan']"
  SET can channels 0 controller "\"mcp2515\"")
can_case(can-standby-polarity-missing "standby and standbyActiveHigh together"
  REMOVE can channels 0 standbyActiveHigh)
can_case(can-shared-pin "a pin not already used by $.can.channels[0].standby"
  SET can channels 1 standby id "\"PB11\"")
can_case(can-pin-not-hard-reserved "a pin covered by a hard gpio reservation"
  REMOVE gpio reservations can-channels pins 8)
can_case(can-zero-bitrate "a uint32 integer of at least 1" SET can channels 0 maxBitrateHz 0)
can_case(can-unknown-field "only fields" SET can channels 0 termination true)
can_case(can-empty "a non-empty channel array" SET can channels "[]")

# ---- Requests board config rejects ----

# A board its target cannot run, a feature symbol outside HAL, a value other
# than 1 in every request form, and an output directory outside the .build
# directory next to the registry, which the output root defaults to.
function(config_rejected expected)
  invoke(2 board config --root "${JH_HAL_SOURCE}" --json ${ARGN})
  string(JSON message GET "${stdout}" message)
  string(FIND "${message}" "${expected}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "board config ${ARGN}: expected '${expected}', got '${message}'")
  endif()
endfunction()
config_rejected("board 'pico2w' is not compatible with target 'rp2350-riscv'"
  --target rp2350-riscv --board pico2w)
config_rejected("feature 'PROJECT_SECRET=value': expected a HAL_ENABLE_* or HAL_DISABLE_* symbol"
  --target rp2040 --board pico --feature PROJECT_SECRET=value)
foreach(request "--feature;HAL_ENABLE_WIFI=0" "--feature;HAL_ENABLE_WIFI=2"
                "--define;HAL_ENABLE_WIFI=0" "--define;HAL_ENABLE_WIFI=2"
                "--define;$<1:HAL_$<1:ENABLE>_WIFI=0>")
  config_rejected("[JH-CFG-VALUE]" --target rp2040 --board pico ${request})
endforeach()
file(REAL_PATH "${JH_HAL_SOURCE}" hal_root)
# Outputs go below the .build directory next to the registry. A registry copy
# in fixtures/ makes that fixtures/.build, so the refused directory beside it
# belongs to this test wherever the build tree lies.
set(outside "${fixtures}/generated-outside-build")
file(REMOVE_RECURSE "${fixtures}/outside-build" "${outside}")
file(COPY "${JH_HAL_SOURCE}/boards/" DESTINATION "${fixtures}/outside-build")
file(REAL_PATH "${fixtures}" fixtures_root)
invoke(2 board config --root "${JH_HAL_SOURCE}" --boards-root "${fixtures}/outside-build"
  --target rp2040 --board pico --json --output-dir "${outside}")
string(JSON message GET "${stdout}" message)
slashed(message "${message}")
string(FIND "${message}" "output directory must be below ${fixtures_root}/.build, got " at)
if(NOT at EQUAL 0 OR EXISTS "${outside}")
  message(FATAL_ERROR "An output directory outside .build was accepted: ${stdout}")
endif()

# ---- Descriptors jh board validate rejects ----

fixture(duplicate-id profiles/rp2040-zero.json SET hal profileId 1)
rejected(duplicate-id "$.hal.profileId")
fixture(duplicate-auto-detect profiles/picow.json
  SET hal autoDetectSelectors "[\"RASPBERRYPI_PICO\"]")
rejected(duplicate-auto-detect "globally unique auto-detection selector")
fixture(ambiguous-legacy-selector profiles/pico.json
  SET hal legacySelectors "[\"HAL_CYW43_PROFILE_PICOW\"]")
rejected(ambiguous-legacy-selector "an unambiguous selector for target 'rp2040'")
fixture(invalid-source-fallback targets/rp2040.json SET sourceFallbackBoard "\"pico2\"")
rejected(invalid-source-fallback "$.sourceFallbackBoard")
fixture(missing-cyw43-component profiles/picow.json REMOVE components cyw43-lwip)
rejected(missing-cyw43-component "CYW43 components")
fixture(unknown-field profiles/pico.json SET typo true)
rejected(unknown-field "only fields")
fixture(invalid-pin profiles/rp2040-zero.json SET gpio reservations status-led pins 0 id 30)
rejected(invalid-pin "target valid pin")
fixture(unknown-capability profiles/pico.json
  SET capabilities unknown-radio "{\"present\": true}")
rejected(unknown-capability "known capability IDs")

# A board device uses a hard-reserved pin only when it owns the reservation,
# so the reset pin cannot become a status LED; the clock pins belong to their
# sources, which carry a known kind and a frequency.
fixture(reset-led profiles/nucleo-g474re.json SET devices statusLed endpoint id "\"PG10\"")
rejected(reset-led "owned by board.status-led")
fixture(clock-pin-unreserved profiles/nucleo-g474re.json REMOVE gpio reservations hse-crystal)
rejected(clock-pin-unreserved "owned by clock.hse")
fixture(clock-pin-foreign-owner profiles/nucleo-g474re.json
  SET gpio reservations lse-crystal owner "\"board.lse\"")
rejected(clock-pin-foreign-owner "owned by clock.lse")
fixture(clock-source-kind profiles/nucleo-g474re.json
  SET clockSources hse kind "\"oscillator\"")
rejected(clock-source-kind "$.clockSources.hse.kind")
fixture(clock-source-frequency profiles/nucleo-g474re.json SET clockSources lse frequencyHz 0)
rejected(clock-source-frequency "$.clockSources.lse.frequencyHz")
# A source lists exactly the GPIOs the target gives its kind (RM0440 7.2: a
# crystal takes OSC_IN and OSC_OUT, a bypass only OSC_IN); an empty or partial
# list would keep the crystal pins out of the reservation check.
fixture(hse-without-pins profiles/nucleo-g474re.json SET clockSources hse pins "[]")
rejected(hse-without-pins "the crystal on hse occupies ['PF0', 'PF1'], the board lists []")
fixture(lse-one-pin profiles/nucleo-g474re.json
  SET clockSources lse pins "[{\"domain\": \"soc-gpio\", \"id\": \"PC14\"}]")
rejected(lse-one-pin "the crystal on lse occupies ['PC14', 'PC15'], the board lists ['PC14']")
fixture(hse-bypass-two-pins profiles/nucleo-g474re.json
  SET clockSources hse kind "\"external-clock\"")
rejected(hse-bypass-two-pins
  "the external-clock on hse occupies ['PF0'], the board lists ['PF0', 'PF1']")
fixture(unknown-clock-input profiles/nucleo-g474re.json
  SET clockSources xosc "{\"kind\": \"crystal\", \"frequencyHz\": 8000000, \"pins\": []}")
rejected(unknown-clock-input "the target has no xosc input for the crystal")
# An HSE bypass drives OSC_IN alone.
fixture(hse-bypass profiles/nucleo-g474re.json
  SET clockSources hse kind "\"external-clock\"" |
  SET clockSources hse pins "[{\"domain\": \"soc-gpio\", \"id\": \"PF0\"}]")
validates(hse-bypass)
fixture(xosc-invented-gpio profiles/pico.json
  SET clockSources xosc pins "[{\"domain\": \"soc-gpio\", \"id\": 0}]")
rejected(xosc-invented-gpio "the crystal on xosc occupies [], the board lists [0]")
fixture(target-without-clock-inputs targets/stm32g474.json REMOVE clockInputs)
rejected(target-without-clock-inputs "$.clockInputs: got None")
fixture(target-clock-input-kind targets/stm32g474.json
  SET clockInputs hse oscillator "[\"PF0\"]")
rejected(target-clock-input-kind "$.clockInputs.hse.oscillator")
fixture(target-clock-input-pin targets/stm32g474.json
  SET clockInputs hse crystal "[\"PF0\", \"PZ1\"]")
rejected(target-clock-input-pin
  "$.clockInputs.hse.crystal: got ['PF0', 'PZ1']; expected target valid pins")

fixture(unknown-component profiles/pico.json
  SET components raw-source "{\"mode\": \"board-owned\"}")
rejected(unknown-component "known component ID")
# Validation reads descriptors only: it does not guess flash facts of an SDK.
fixture(flash-mismatch profiles/rp2040-plus-4mb.json SET memory flash expectedBytes 2097152)
validates(flash-mismatch)
fixture(psram-mismatch profiles/waveshare-esp32-s3-zero.json
  SET memory psram sizeBytes 67108864)
rejected(psram-mismatch "external RAM supported by target esp32s3")

# The USB facts of a configuration come from the target descriptor, not from
# its MCU name; a malformed usbDevice fails there.
fixture(usb-device-defaults targets/stm32g474.json
  SET usbDevice "{\"vid\": 4660, \"pid\": 22136}")
configure_fixture(usb-device-defaults stm32g474 nucleo-g474re)
file(READ "${fixtures}/.build/usb-device-defaults/jh_board_config.h" usb_header)
contains("${usb_header}" "#define HAL_TARGET_USB_VID UINT16_C(4660)" "Target USB facts")
contains("${usb_header}" "#define HAL_TARGET_USB_PID UINT16_C(22136)" "Target USB facts")
foreach(usb "bool;{\"vid\": true, \"pid\": 9}" "overflow;{\"vid\": 11914, \"pid\": 65536}"
            "negative;{\"vid\": -1, \"pid\": 9}" "missing;{\"vid\": 11914}")
  list(GET usb 0 case)
  list(GET usb 1 value)
  fixture(usb-device-${case} targets/rp2350-arm.json SET usbDevice "${value}")
  rejected(usb-device-${case} "usbDevice")
endforeach()

# The features a target requires must be supported, and the supported set
# must hold its own closure.
fixture(unsupported-required-feature targets/esp32s3.json SET supportedFeatures "[]")
rejected(unsupported-required-feature "a superset of target-required features")
file(READ "${JH_HAL_SOURCE}/boards/targets/esp32s3.json" esp32s3_target)
string(JSON supported_count LENGTH "${esp32s3_target}" supportedFeatures)
math(EXPR supported_last "${supported_count} - 1")
set(network_core -1)
foreach(index RANGE ${supported_last})
  string(JSON feature GET "${esp32s3_target}" supportedFeatures ${index})
  if(feature STREQUAL "HAL_ENABLE_NETWORK_CORE")
    set(network_core ${index})
  endif()
endforeach()
if(network_core EQUAL -1)
  message(FATAL_ERROR "esp32s3 does not support HAL_ENABLE_NETWORK_CORE")
endif()
fixture(incomplete-supported-closure targets/esp32s3.json
  REMOVE supportedFeatures ${network_core})
rejected(incomplete-supported-closure "missing closure ['HAL_ENABLE_NETWORK_CORE']")

# ---- Bus devices ----

# A typed multi-pin bus device: rp2040-zero gains a synthetic SX1262 radio,
# so the role model is covered before any board profile ships one. Its
# signals take pins 13-19, 23 and 24; pin 20 is reserved for the radio too.
set(radio_device [=[{"kind": "bus-device", "role": "sx1262-radio",
 "bus": {"kind": "spi", "index": 1},
 "signals": {"sck": {"domain": "soc-gpio", "id": 14}, "mosi": {"domain": "soc-gpio", "id": 15},
  "miso": {"domain": "soc-gpio", "id": 24}, "cs": {"domain": "soc-gpio", "id": 13},
  "reset": {"domain": "soc-gpio", "id": 23}, "busy": {"domain": "soc-gpio", "id": 18},
  "dio1": {"domain": "soc-gpio", "id": 19}, "rfSwitchA": {"domain": "soc-gpio", "id": 17}},
 "attributes": {"minFrequencyHz": 410000000, "maxFrequencyHz": 525000000,
  "maxSpiClockHz": 16000000, "defaultSpiClockHz": 8000000, "minTxPowerDbm": -9,
  "maxTxPowerDbm": 22, "regulator": "dcdc", "rfSwitchMode": "single-gpio",
  "rfSwitchIdleLevelA": true, "rfSwitchRxLevelA": true, "rfSwitchTxLevelA": false,
  "tcxoControl": "dio3", "tcxoVoltage": "1v8", "tcxoStartupUs": 5000}}]=])
set(radio_pins "")
foreach(pin 13 14 15 17 18 19 20 23 24)
  list(APPEND radio_pins "{\"domain\": \"soc-gpio\", \"id\": ${pin}}")
endforeach()
list(JOIN radio_pins ", " radio_pins)
set(radio_reservation "{\"pins\": [${radio_pins}], \"owner\": \"board.lora-radio\", \"strength\": \"hard\", \"reason\": \"Synthetic SX1262 radio wiring under test.\"}")
# The radio board at fixtures/case, its reservation unless reserve is OFF,
# then the operations of ARGN on the device, with paths below it.
function(radio_fixture case reserve)
  set(edits SET devices loraRadio "${radio_device}")
  if(reserve)
    list(APPEND edits | SET gpio reservations lora-radio "${radio_reservation}")
  endif()
  fixture(${case} profiles/rp2040-zero.json ${edits})
  edit_json("${fixtures}/${case}" profiles/rp2040-zero.json PREFIX devices loraRadio
    OPERATIONS ${ARGN})
endfunction()
function(radio_rejected case expected)
  radio_fixture(${case} ON ${ARGN})
  rejected(${case} "${expected}")
endfunction()
set(drop_switch_levels REMOVE attributes rfSwitchIdleLevelA |
  REMOVE attributes rfSwitchRxLevelA | REMOVE attributes rfSwitchTxLevelA)

radio_fixture(radio-valid ON)
validates(radio-valid)
configure_fixture(radio-valid rp2040 rp2040-zero)
set(radio_output "${fixtures}/.build/radio-valid")
file(READ "${radio_output}/jh_board_config.h" radio_header)
# The configuration depends on the copy it read and on the component
# catalogue of the checkout, never on the tracked registry.
file(STRINGS "${radio_output}/generation.d" generation_inputs)
file(REAL_PATH "${fixtures}/radio-valid" radio_root)
foreach(input "${radio_root}/capabilities.json" "${radio_root}/profiles/rp2040-zero.json"
              "${hal_root}/config/tooling/board_components.json")
  if(NOT input IN_LIST generation_inputs)
    message(FATAL_ERROR "generation.d lacks ${input}: ${generation_inputs}")
  endif()
endforeach()
if("${hal_root}/boards/capabilities.json" IN_LIST generation_inputs)
  message(FATAL_ERROR "generation.d lists the tracked boards instead of --boards-root")
endif()
file(READ "${radio_output}/jh_board_config.cmake" radio_cmake)
contains("${radio_cmake}"
  "set(JH_BOARD_INPUT_GLOBS \"${radio_root}/*.json;${hal_root}/config/*.json\")"
  "The input patterns of --boards-root")
foreach(expected
    "#define HAL_BOARD_DEVICE_PIN_NONE 0xFFu"
    "#define HAL_BOARD_LORA_RADIO_PRESENT 1"
    "#define HAL_BOARD_LORA_RADIO_SPI_BUS 1u"
    "#define HAL_BOARD_LORA_RADIO_PIN_CS 13u"
    "#define HAL_BOARD_LORA_RADIO_PIN_DIO1 19u"
    "#define HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_A 17u"
    "#define HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_B HAL_BOARD_DEVICE_PIN_NONE"
    "#define HAL_BOARD_LORA_RADIO_MIN_FREQUENCY_HZ UINT32_C(410000000)"
    "#define HAL_BOARD_LORA_RADIO_MIN_TX_POWER_DBM (-9)"
    "#define HAL_BOARD_LORA_RADIO_DEFAULT_SPI_CLOCK_HZ UINT32_C(8000000)"
    "#define HAL_BOARD_LORA_RADIO_REGULATOR_IS_DCDC 1"
    "#define HAL_BOARD_LORA_RADIO_REGULATOR_IS_LDO 0"
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_SINGLE_GPIO 1"
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_DUAL_GPIO 0"
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_TX_LEVEL_A 0"
    "#define HAL_BOARD_LORA_RADIO_TCXO_CONTROL_IS_DIO3 1"
    "#define HAL_BOARD_LORA_RADIO_TCXO_VOLTAGE_IS_1V8 1"
    "#define HAL_BOARD_LORA_RADIO_TCXO_STARTUP_US UINT32_C(5000)")
  contains("${radio_header}" "${expected}" "Radio board header")
endforeach()
file(READ "${radio_output}/jh_board_resolved.json" radio_resolved)
json_equals("${radio_resolved}" "${radio_device}" "The resolved bus device" devices loraRadio)

radio_rejected(radio-missing-signal "$.devices.loraRadio.signals" REMOVE signals busy)
radio_rejected(radio-unknown-signal "$.devices.loraRadio.signals"
  SET signals dio9 "{\"domain\": \"soc-gpio\", \"id\": 20}")
radio_rejected(radio-duplicate-pin "a pin not already used by cs"
  SET signals dio1 "{\"domain\": \"soc-gpio\", \"id\": 13}")
radio_fixture(radio-unreserved-pin OFF)
rejected(radio-unreserved-pin "a pin covered by a hard gpio reservation")
radio_rejected(radio-unordered-frequency "$.devices.loraRadio.attributes.minFrequencyHz"
  SET attributes maxFrequencyHz 400000000)
radio_rejected(radio-clock-above-max "$.devices.loraRadio.attributes.defaultSpiClockHz"
  SET attributes defaultSpiClockHz 20000000)
radio_rejected(radio-missing-attribute "required sx1262-radio attributes"
  REMOVE attributes regulator)
radio_rejected(radio-unknown-attribute
  "only attributes required by the active sx1262-radio configuration" SET attributes bogus 1)
radio_rejected(radio-bad-enum "$.devices.loraRadio.attributes.regulator"
  SET attributes regulator "\"buck\"")
radio_rejected(radio-int8-range "an int8 integer in [-128, 127]"
  SET attributes maxTxPowerDbm 500)
radio_rejected(radio-bool-type "a boolean" SET attributes rfSwitchTxLevelA 1)
radio_rejected(radio-unknown-role "a known device role" SET role "\"sx1276-radio\"")
radio_rejected(radio-bad-bus-kind "$.devices.loraRadio.bus.kind" SET bus kind "\"i2c\"")
radio_rejected(radio-unknown-kind "$.devices.loraRadio.kind" SET kind "\"spi-thing\"")
fixture(radio-non-camel-id profiles/rp2040-zero.json SET devices lora_radio
  "{\"kind\": \"gpio\", \"endpoint\": {\"domain\": \"soc-gpio\", \"id\": 20}}")
rejected(radio-non-camel-id "a camelCase device ID")
radio_rejected(radio-switch-signal-without-mode
  "only signals required by the active sx1262-radio configuration"
  SET attributes rfSwitchMode "\"dio2\"" | ${drop_switch_levels})
radio_rejected(radio-tcxo-none-keeps-voltage
  "only attributes required by the active sx1262-radio configuration"
  SET attributes tcxoControl "\"none\"")
radio_rejected(radio-tcxo-dio3-without-voltage "required sx1262-radio attributes"
  REMOVE attributes tcxoVoltage)

# Switch and TCXO configurations without the signals and attributes they do
# not use.
radio_fixture(radio-dio2-switch ON SET attributes rfSwitchMode "\"dio2\"" |
  ${drop_switch_levels} | REMOVE signals rfSwitchA)
validates(radio-dio2-switch)
radio_fixture(radio-dio2-single-gpio-switch ON SET attributes rfSwitchMode "\"dio2-single-gpio\"")
validates(radio-dio2-single-gpio-switch)
radio_fixture(radio-tcxo-none ON SET attributes tcxoControl "\"none\"" |
  REMOVE attributes tcxoVoltage | REMOVE attributes tcxoStartupUs)
validates(radio-tcxo-none)
radio_fixture(radio-dual-gpio-switch ON SET attributes rfSwitchMode "\"dual-gpio\"" |
  SET attributes rfSwitchTxLevelA true | SET attributes rfSwitchIdleLevelB false |
  SET attributes rfSwitchRxLevelB true | SET attributes rfSwitchTxLevelB false |
  SET signals rfSwitchB "{\"domain\": \"soc-gpio\", \"id\": 20}")
configure_fixture(radio-dual-gpio-switch rp2040 rp2040-zero)
file(READ "${fixtures}/.build/radio-dual-gpio-switch/jh_board_config.h" dual_header)
foreach(expected
    "#define HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_A 17u"
    "#define HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_B 20u"
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_DUAL_GPIO 1"
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_SINGLE_GPIO 0"
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_TX_LEVEL_A 1"
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_IDLE_LEVEL_B 0"
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_RX_LEVEL_B 1"
    "#define HAL_BOARD_LORA_RADIO_RF_SWITCH_TX_LEVEL_B 0"
    "#define HAL_BOARD_LORA_RADIO_TCXO_CONTROL_IS_DIO3 1")
  contains("${dual_header}" "${expected}" "Dual-GPIO switch header")
endforeach()

# ---- With a compiler ----

# A GCC or Clang compiler of this build; on Windows, as there is none, the
# gnu-arm toolchain runmefirst recorded, which links without a C library.
set(cc "")
set(cross OFF)
if(JH_CC AND JH_CC_ID MATCHES "GNU|Clang" AND EXISTS "${JH_AR}")
  set(cc "${JH_CC}")
  set(ar "${JH_AR}")
elseif(CMAKE_HOST_WIN32 AND EXISTS "${JH_HAL_SOURCE}/.build/windows/host-environment.json")
  file(READ "${JH_HAL_SOURCE}/.build/windows/host-environment.json" host_state)
  string(JSON arm_gcc ERROR_VARIABLE missing GET "${host_state}" tools gnu-arm)
  if(NOT missing AND EXISTS "${arm_gcc}")
    get_filename_component(arm_bin "${arm_gcc}" DIRECTORY)
    if(EXISTS "${arm_bin}/arm-none-eabi-ar.exe")
      set(cc "${arm_gcc}")
      set(ar "${arm_bin}/arm-none-eabi-ar.exe")
      set(cross ON)
    endif()
  endif()
endif()
if(cc STREQUAL "")
  message(FATAL_ERROR "jh_boards needs a GCC or Clang compiler with its archiver, or the "
    "gnu-arm toolchain of .build/windows/host-environment.json")
endif()
set(link_options -Wl,--gc-sections)
if(cross)
  list(PREPEND link_options -nostdlib -Wl,-e,main)
endif()

# The object-like board macros hal/system/hal_board.h defines with the include
# directories and definitions of ARGN, sorted, as text.
set(probe "${JH_WORK}/board-probe.c")
file(WRITE "${probe}" "#include \"hal/system/hal_board.h\"\n")
function(board_macros variable)
  execute_process(COMMAND "${cc}" -std=c17 -dM -E -x c ${ARGN}
    -I "${JH_HAL_SOURCE}/src" -I "${JH_JHBASE}/include" "${probe}"
    RESULT_VARIABLE code OUTPUT_VARIABLE dump ERROR_VARIABLE errors)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "hal_board.h with ${ARGN} failed:\n${errors}")
  endif()
  string(REPLACE ";" "<semicolon>" dump "${dump}")
  string(REPLACE "\n" ";" dump "${dump}")
  set(macros "")
  foreach(line IN LISTS dump)
    # Separate statements: if() evaluates a parenthesized MATCHES first.
    if(NOT line MATCHES "^#define ([A-Za-z0-9_]+)( .*)?$")
      continue()
    endif()
    set(name "${CMAKE_MATCH_1}")
    if(name STREQUAL "HAL_BOARD_PROFILE_TARGET")
      continue()
    endif()
    if(name MATCHES "^(HAL_BOARD_|HAL_CYW43_PROFILE_|HAL_NETWORK_BACKEND_|HAL_CYW43_BUS_|HAL_CYW43_STACK_|HAL_CYW43_PIN_|HAL_CYW43_MAX_)" OR
       name STREQUAL "HAL_LED_BUILTIN" OR name STREQUAL "LED_BUILTIN")
      list(APPEND macros "${line}")
    endif()
  endforeach()
  list(SORT macros)
  list(JOIN macros "\n" macros)
  set(${variable} "${macros}" PARENT_SCOPE)
endfunction()

# The fallback header selects, for each compatible pair of target and board,
# the board macros the generated configuration of that pair defines.
file(GLOB profiles "${JH_HAL_SOURCE}/boards/profiles/*.json")
list(SORT profiles)
set(pairs 0)
foreach(profile IN LISTS profiles)
  file(READ "${profile}" descriptor)
  string(JSON board GET "${descriptor}" id)
  string(JSON board_selector GET "${descriptor}" hal selector)
  string(JSON compatible LENGTH "${descriptor}" compatibleTargets)
  math(EXPR last "${compatible} - 1")
  foreach(index RANGE ${last})
    string(JSON target GET "${descriptor}" compatibleTargets ${index})
    file(READ "${JH_HAL_SOURCE}/boards/targets/${target}.json" target_descriptor)
    string(JSON target_selector GET "${target_descriptor}" hal targetSelector)
    configure(parity-${target}-${board} ${target} ${board})
    board_macros(generated_macros -I "${generated}/parity-${target}-${board}"
      -D${target_selector}=1)
    board_macros(fallback_macros -D${target_selector}=1 -D${board_selector}=1)
    if(NOT generated_macros STREQUAL fallback_macros)
      message(FATAL_ERROR "The fallback differs from the generated configuration of "
        "${target}/${board}:\n${fallback_macros}\n---\n${generated_macros}")
    endif()
    math(EXPR pairs "${pairs} + 1")
  endforeach()
endforeach()
if(pairs EQUAL 0)
  message(FATAL_ERROR "No compatible target and board pair was compared")
endif()

# A board library links only with the contract of the same board and feature
# closure, and a missing or other contract fails naming the expected symbol,
# also when section garbage collection drops unreferenced code.
set(link "${JH_WORK}/link-contract")
file(MAKE_DIRECTORY "${link}")
file(WRITE "${link}/main.c" "int main(void) { return 0; }\n")
function(compile_object source object include)
  execute_process(COMMAND "${cc}" -std=c17 -Wall -Wextra -Werror -ffunction-sections
    -fdata-sections -I "${include}" -c "${source}" -o "${link}/${object}"
    RESULT_VARIABLE code ERROR_VARIABLE errors)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "Compiling ${source} failed:\n${errors}")
  endif()
endfunction()
function(archive name object)
  execute_process(COMMAND "${ar}" rcs "${link}/${name}" "${link}/${object}"
    RESULT_VARIABLE code ERROR_VARIABLE errors)
  if(NOT code EQUAL 0)
    message(FATAL_ERROR "Archiving ${object} failed:\n${errors}")
  endif()
endfunction()
# Link main and the reference with the archives of ARGN; code receives the
# exit code, errors the diagnostics.
function(link_program output code_variable errors_variable)
  set(archives "")
  foreach(name ${ARGN})
    list(APPEND archives "${link}/${name}")
  endforeach()
  execute_process(COMMAND "${cc}" ${link_options} "${link}/main.o" "${link}/reference.o"
    ${archives} -o "${link}/${output}"
    RESULT_VARIABLE code OUTPUT_QUIET ERROR_VARIABLE errors)
  set(${code_variable} "${code}" PARENT_SCOPE)
  set(${errors_variable} "${errors}" PARENT_SCOPE)
endfunction()
compile_object("${generated}/first/jh_link_contract_definition.c" definition.o "${generated}/first")
compile_object("${generated}/first/jh_link_contract_reference.c" reference.o "${generated}/first")
compile_object("${link}/main.c" main.o "${generated}/first")
archive(libmatching.a definition.o)
link_program(matching code errors libmatching.a)
if(NOT code EQUAL 0)
  message(FATAL_ERROR "The matching board contract did not link:\n${errors}")
endif()
link_program(missing-contract code errors)
if(code EQUAL 0)
  message(FATAL_ERROR "A missing board contract linked with section GC enabled")
endif()
contains("${errors}" "${contract_symbol}" "The missing-contract link failure")
configure(plus rp2040 rp2040-plus-4mb)
compile_object("${generated}/plus/jh_link_contract_definition.c" wrong-definition.o "${generated}/plus")
archive(libwrong-board.a wrong-definition.o)
link_program(wrong-board code errors libwrong-board.a)
if(code EQUAL 0)
  message(FATAL_ERROR "An archive of another board linked")
endif()
contains("${errors}" "${contract_symbol}" "The wrong-board link failure")
configure(different-feature rp2040 rp2040-zero --feature HAL_ENABLE_WIFI)
compile_object("${generated}/different-feature/jh_link_contract_definition.c"
  different-feature.o "${generated}/different-feature")
archive(libdifferent-feature.a different-feature.o)
link_program(different-feature code errors libdifferent-feature.a)
if(code EQUAL 0)
  message(FATAL_ERROR "An archive with a different feature set linked")
endif()
