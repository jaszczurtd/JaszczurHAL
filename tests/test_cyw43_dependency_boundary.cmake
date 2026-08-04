set(_driver "${JH_ROOT}/src/hal/impl/shared/drivers/cyw43-driver")

file(READ "${_driver}/cyw43_configport.h" _cyw43_port_config)
if(NOT _cyw43_port_config MATCHES
   "#define[ \t]+CYW43_USE_OTP_MAC[ \t]+\\(1\\)")
    message(FATAL_ERROR
        "CYW43 port must prefer the radio OTP MAC before the UID fallback")
endif()

file(READ "${_driver}/jh_cyw43_driver.cpp" _cyw43_driver_wrapper)
if(NOT _cyw43_driver_wrapper MATCHES
   "memcpy\\(mac,[ \t]*cyw43_state\\.mac,[ \t]*sizeof\\(cyw43_state\\.mac\\)\\)")
    message(FATAL_ERROR
        "CYW43 port must expose the OTP/fallback MAC stored by the controller")
endif()
if(_cyw43_driver_wrapper MATCHES "uint8_t[ \t]+s_mac\\[6\\]")
    message(FATAL_ERROR
        "CYW43 port must not shadow the controller MAC with a separate cache")
endif()

string(FIND "${_cyw43_driver_wrapper}"
    "jh_cyw43_port_pin_read" _cyw43_pin_read_begin)
string(FIND "${_cyw43_driver_wrapper}"
    "jh_cyw43_port_pin_low" _cyw43_pin_read_end)
string(FIND "${_cyw43_driver_wrapper}"
    "jh_cyw43_gspi_host_wake_refresh(s_transport)" _cyw43_refresh)
if(_cyw43_pin_read_begin LESS 0 OR _cyw43_pin_read_end LESS 0 OR
   _cyw43_refresh LESS _cyw43_pin_read_begin OR
   _cyw43_refresh GREATER _cyw43_pin_read_end)
    message(FATAL_ERROR
        "CYW43 pin read must refresh polled HOST_WAKE before testing its latch")
endif()

set(_pinned_files
    "vendor/src/cybt_logging.h|ab0ac639e72b5a2797c54e704a33bd3d4a74be9250b4b12df6e5856b021103de"
    "vendor/src/cybt_shared_bus.c.upstream|bab75c7d42b25d9b3b1e04e59ffc1c38ac807ae8b5682c405c69e7f9d9f5b75f"
    "vendor/src/cybt_shared_bus_driver.c.upstream|4b62a35f7b5b5a4e991e636986e95ee5a9d1fddbed964cc6d9dd9a5307aaa481"
    "vendor/src/cybt_shared_bus_driver.h|c65edbb4152de9f78ceebebf1e0e306638c769387e1d6c2ef7836d35bbcea37d"
    "vendor/src/cyw43.h|d315bcfe96ca96b0309d760fe93a60f11ed8b44b7da2472f5ba395ef19985ba0"
    "vendor/src/cyw43_btbus.h|4e7d8ac7e49e328d957f4166fcffe7cdae56b784fbdcafc59641969ab9f2de30"
    "vendor/src/cyw43_config.h|0a3b03ab983d6afce154323bb887943344ddb9658f8a4cd2b29b79b847dca03a"
    "vendor/src/cyw43_country.h|e684a5e8a53de190380090a11e8af714986cc0287765a04fccd04ab4cb04767f"
    "vendor/src/cyw43_debug_pins.h|3f5904db36f90baa3f98f10f662e681c43a2ce55f116b1546620b157b05fa569"
    "vendor/src/cyw43_internal.h|6d03134bcb26108494ea1b8bca0c8d2e4e7f75dd1b6e79d4b54fcc6346457e64"
    "vendor/src/cyw43_ll.h|67ecbbd1d5b7c088a5fb7fcb04fcfd9189cdf36a471515285d0092118546e802"
    "vendor/src/cyw43_sdio.h|f9066b3b1d18b66d2ca09fc2ab9ffe98e71fc49c1f3803699ed614b8a0110460"
    "vendor/src/cyw43_spi.h|d8115519fa0010f4c902bcc339f44483b464ddbc89d3dffee375a485a2ce170f"
    "vendor/src/cyw43_stats.h|8d643fa33f584a72a28f12d27763c94bf4190130e54cf4c1761eda0678af5c98"
    "vendor/src/cyw43_bthci_uart.c.upstream|070966cc4e3af48d5c3415ca5855eb5d2b718f1d62cd3e6232cc2d98f17c8b08"
    "vendor/src/cyw43_ctrl.c.upstream|97471aef5270af9f000d0e4f14ad7753967a425a63b0ca07269f7f1228eb67f1"
    "vendor/src/cyw43_ll.c.upstream|7f4c93755b2ab911d18606652497b2f3cd08085ce63b3b1836171d375f806b10"
    "vendor/src/cyw43_lwip.c.upstream|8d078e9e162211858d8061134301cc151674e4269af634750fd0677747f526af"
    "vendor/src/cyw43_sdio.c.upstream|613ae42671688470fef68e61fa923b70f3ca57406fe24f9a01eecd1fb65935d8"
    "vendor/src/cyw43_spi.c.upstream|c6cf5c5d41ca51fc461ef1ab66d62776a522b8f16eb8bc397bf0d5e18b0cd72d"
    "vendor/src/cyw43_stats.c.upstream|a13d8ac3c6bc82bba7f314cb5dadfe9389da12176108759568eac4c36fa9c341"
    "vendor/firmware/cyw43_btfw_1yn.h|ea868fe5c47bc49731a213bc46e841e4e495c4c8fa9b6a4f3ca821336eea4ace"
    "vendor/firmware/cyw43_btfw_43439.h|d05a8dca7e232ada470d8ed723edaa1ea78000098870f9b979d490d83ad35b22"
    "vendor/firmware/cyw43_btfw_4343A1.h|5ab5fce26ae38224c80fd78ae2c5890bcb60459dc327bc4456e538904e4ff80b"
    "vendor/firmware/w43439A0_7_95_49_00_combined.h|120bb63db1c5cf42cd44cf9a057d6f14601038a9413badf5a3b737f6552bc9ea"
    "vendor/firmware/w43439_sdio_1yn_7_95_59_combined.h|c821790f21c25a1eacd3bdc03a48198fbd8763fc471aa3cb61c3232f0eee0107"
    "vendor/firmware/w4343WA1_7_45_98_102_combined.h|407b6ccfd3ece0ba8ca5e800a2cc51c39ef44176aa1007bfbed6b4c7f2276c00"
    "vendor/firmware/w4343WA1_7_45_98_50_combined.h|151fcd14077e8efe82fdcd4df2166ac7b1d3d347bc245dba5a3c71ead1bc18a1"
    "vendor/firmware/wb43439A0_7_95_49_00_combined.h|6b4b9a717c3c5d669b56a3bc38b58df3901f63e3a7363ccf5d74706460762176"
    "vendor/firmware/wifi_nvram_1dx.h|f000c335ca2b9924b1923642a2360428f67208bbcc63f3b05b673dea58c532ff"
    "vendor/firmware/wifi_nvram_1yn.h|41d706aff7001481c8ff38e37eb2ddef9d795fefb099860ce820570457f59466"
    "vendor/firmware/wifi_nvram_43439.h|ddc82b00667643f55d7687bba1b5e4028f24717ec8883f05bd7d435f4c9c01ad"
)

set(_pinned_paths)
foreach(_entry IN LISTS _pinned_files)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _relative_path)
    list(GET _parts 1 _expected_sha256)
    list(APPEND _pinned_paths "${_relative_path}")
    set(_path "${_driver}/${_relative_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing pinned CYW43 dependency: ${_path}")
    endif()
    file(SHA256 "${_path}" _actual_sha256)
    if(NOT _actual_sha256 STREQUAL _expected_sha256)
        message(FATAL_ERROR
            "CYW43 dependency drift in ${_relative_path}: ${_actual_sha256}")
    endif()
endforeach()

file(GLOB _vendored_upstream_files RELATIVE "${_driver}"
    "${_driver}/vendor/src/*.h"
    "${_driver}/vendor/src/*.c.upstream"
    "${_driver}/vendor/firmware/*.h")
list(SORT _pinned_paths)
list(SORT _vendored_upstream_files)
if(NOT _vendored_upstream_files STREQUAL _pinned_paths)
    message(FATAL_ERROR
        "CYW43 checksum manifest does not cover the complete vendored import")
endif()

file(GLOB_RECURSE _implicit_sources "${_driver}/vendor/*.c")
if(_implicit_sources)
    message(FATAL_ERROR
        "Vendored CYW43 .c files would be compiled implicitly: ${_implicit_sources}")
endif()

set(_backend_sources
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_platform.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_provider.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_gspi.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/hal_ota.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/hal_tcp.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/hal_time.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/hal_udp.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/rp2040_lwip_extension_port.cpp"
    "${JH_ROOT}/src/hal/hal_net.cpp"
    "${JH_ROOT}/src/hal/hal_tcp.cpp"
    "${JH_ROOT}/src/hal/hal_udp.cpp"
    "${JH_ROOT}/src/hal/hal_wifi.cpp"
)
foreach(_source IN LISTS _backend_sources)
    file(READ "${_source}" _contents)
    if(_contents MATCHES
       "#[ \t]*include[ \t]*[<\"](Arduino|ArduinoOTA|WiFi|WiFiNTP|ClientContext|pico/cyw43_arch)")
        message(FATAL_ERROR "Arduino network API leaked into ${_source}")
    endif()
    if(_contents MATCHES "#[ \t]*include[ \t]*<cyw43\\.h>")
        message(FATAL_ERROR "Carrier-owned cyw43.h include leaked into ${_source}")
    endif()
endforeach()

foreach(_source IN ITEMS
        "${JH_ROOT}/src/hal/hal_wifi.cpp"
        "${JH_ROOT}/src/hal/hal_net.cpp"
        "${JH_ROOT}/src/hal/hal_tcp.cpp"
        "${JH_ROOT}/src/hal/hal_udp.cpp"
        "${JH_ROOT}/src/hal/impl/shared/network/mqtt/hal_mqtt.cpp"
        "${JH_ROOT}/src/hal/impl/shared/network/wireguard/hal_wireguard.cpp")
    file(READ "${_source}" _contents)
    if(NOT _contents MATCHES "jh_network_require_(hardware|ready)")
        message(FATAL_ERROR
            "Public CYW43 runtime preflight is missing in ${_source}")
    endif()
endforeach()

foreach(_source IN ITEMS
        "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_platform.cpp"
        "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_provider.cpp")
    file(READ "${_source}" _contents)
    if(NOT _contents MATCHES
       "drivers/cyw43-driver/jh_cyw43_driver\\.h")
        message(FATAL_ERROR "Pinned CYW43 include boundary missing in ${_source}")
    endif()
endforeach()

file(READ
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_platform.cpp"
    _rp2040_platform)
foreach(_required IN ITEMS
        "s_state_mutex"
        "s_stack_mutex"
        "platform_state_lock"
        "platform_stack_enter")
    if(NOT _rp2040_platform MATCHES "${_required}")
        message(FATAL_ERROR
            "RP2040 network-service lock separation is missing '${_required}'")
    endif()
endforeach()

if(EXISTS "${_driver}/jh_cyw43_namespace.h")
    message(FATAL_ERROR
        "Obsolete carrier coexistence namespace must not remain")
endif()
if(EXISTS
   "${JH_ROOT}/src/hal/impl/rp2040/rp2040_arduino_network_backend.cpp")
    message(FATAL_ERROR
        "Removed RP network backend source must not remain")
endif()
foreach(_removed_source IN ITEMS
        "${JH_ROOT}/src/hal/impl/rp2040/hal_net.cpp"
        "${JH_ROOT}/src/hal/impl/rp2040/hal_wifi.cpp")
    if(EXISTS "${_removed_source}")
        message(FATAL_ERROR
            "Removed RP network implementation must not remain: ${_removed_source}")
    endif()
endforeach()
if(NOT EXISTS
   "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/CYW43_PIO_UPSTREAM.md")
    message(FATAL_ERROR "RP2040 CYW43 PIO provenance metadata is missing")
endif()

foreach(_removed_path IN ITEMS
        "${JH_ROOT}/cmake/targets/rp2040.cmake"
        "${JH_ROOT}/cmake/rp2040_core_wrap_non_network.txt"
        "${JH_ROOT}/library.properties"
        "${JH_ROOT}/rp2040_core_version.conf"
        "${JH_ROOT}/rp2040_lib"
        "${JH_ROOT}/scripts/build_rp2040_lib.sh"
        "${JH_ROOT}/vscode/neutral_fw/rp2040_arduino_pico"
        "${JH_ROOT}/vscode/targets/rp2040-arduino.json")
    if(EXISTS "${_removed_path}")
        message(FATAL_ERROR "Removed RP carrier path remains: ${_removed_path}")
    endif()
endforeach()

file(READ "${JH_ROOT}/boards/profiles/pico-rm2.json" _rp2040_profiles)
file(READ "${JH_ROOT}/boards/profiles/picow.json" _picow_profile)
file(READ "${JH_ROOT}/boards/profiles/pico2w.json" _pico2w_profile)
string(APPEND _rp2040_profiles
    "\n${_picow_profile}\n${_pico2w_profile}")
foreach(_required IN ITEMS
        "\"id\": \"pico-rm2\""
        "\"id\": \"picow\""
        "\"id\": \"pico2w\""
        "HAL_BOARD_PROFILE_RP_PICO_PIM730"
        "HAL_BOARD_PROFILE_RP_PICO_W"
        "HAL_BOARD_PROFILE_RP_PICO_2_W"
        "\"cyw43-lwip\"")
    if(NOT _rp2040_profiles MATCHES "${_required}")
        message(FATAL_ERROR
            "RP2040 board profiles are missing '${_required}'")
    endif()
endforeach()
foreach(_forbidden IN ITEMS
        "picow-shared"
        "JH_RP2040_BOARD_DEFINES"
        "PICO_CYW43_SUPPORTED"
        "CYW43_PIN_WL_DYNAMIC"
        "CYW43_PIO_CLOCK_DIV_DYNAMIC")
    if(_rp2040_profiles MATCHES "${_forbidden}")
        message(FATAL_ERROR
            "RP2040 board profiles retain carrier network token '${_forbidden}'")
    endif()
endforeach()
