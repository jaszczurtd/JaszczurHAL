set(_driver "${JH_ROOT}/src/hal/network/cyw43")

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

file(READ "${_driver}/jh_cyw43_hostname.cpp" _cyw43_hostname)
file(READ "${_driver}/jh_cyw43_mdns.cpp" _cyw43_mdns)
file(READ "${JH_ROOT}/cmake/jh_cyw43_driver.cmake" _cyw43_cmake)
file(READ "${JH_ROOT}/cmake/jh_rp_native_sdk.cmake" _rp_native_cmake)
file(READ "${JH_ROOT}/cmake/targets/stm32g474.cmake"
    _stm32_firmware_cmake)
file(READ "${JH_ROOT}/stm32_lib/CMakeLists.txt" _stm32_library_cmake)
file(READ "${JH_ROOT}/src/hal/network/lwip/port/lwipopts.h" _lwipopts)
file(READ
    "${JH_ROOT}/src/hal/network/lwip/port/jh_lwip_mdns_adapter.inc"
    _mdns_adapter)
file(READ
    "${JH_ROOT}/src/hal/network/lwip/port/jh_lwip_mdns_teardown.inc"
    _mdns_teardown)
file(READ "${JH_ROOT}/src/hal/impl/rp2040/hal_ota.cpp" _rp_ota)
file(READ
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_provider.cpp"
    _rp_provider)
file(READ
    "${JH_ROOT}/src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_cyw43_network_backend.cpp"
    _stm32_provider)

foreach(_hostname_contract IN ITEMS
        "netif_set_hostname"
        "dhcp_supplied_address"
        "dhcp_renew")
    string(FIND "${_cyw43_hostname}" "${_hostname_contract}"
        _hostname_contract_at)
    if(_hostname_contract_at EQUAL -1)
        message(FATAL_ERROR
            "CYW43 DHCP hostname contract is missing: ${_hostname_contract}")
    endif()
endforeach()

foreach(_hostname_provider IN ITEMS _rp_provider _stm32_provider)
    if(NOT "${${_hostname_provider}}" MATCHES "jh_cyw43_hostname_apply")
        message(FATAL_ERROR
            "CYW43 backend bypasses the shared DHCP hostname helper: "
            "${_hostname_provider}")
    endif()
endforeach()

foreach(_mdns_source IN ITEMS
        "src/apps/mdns/mdns.c"
        "src/apps/mdns/mdns_domain.c"
        "src/apps/mdns/mdns_out.c")
    string(FIND "${_cyw43_cmake}" "${_mdns_source}" _mdns_source_at)
    if(_mdns_source_at EQUAL -1)
        message(FATAL_ERROR
            "CYW43 mDNS source manifest is missing: ${_mdns_source}")
    endif()
endforeach()

foreach(_mdns_contract IN ITEMS
        "if(JH_CYW43_FEATURE_OTA)"
        "list(APPEND _jh_cyw43_options MDNS)"
        "LWIP_MDNS_RESPONDER 1"
        "LWIP_MDNS_SEARCH 0"
        "LWIP_NUM_NETIF_CLIENT_DATA 1"
        "LWIP_NETIF_EXT_STATUS_CALLBACK 1"
        "MDNS_MAX_STORED_PKTS 4"
        "JH_LWIP_MDNS_SYS_TIMEOUT_RESERVE (6 + MDNS_MAX_STORED_PKTS)"
        "JH_LWIP_WIREGUARD_SYS_TIMEOUT_RESERVE"
        "JH_LWIP_MDNS_SYS_TIMEOUT_RESERVE"
        "_jh_generated_mdns_core"
        "jh_lwip_mdns_upstream.inc"
        "jh_lwip_mdns_teardown.inc"
        "-fconserve-stack"
        "jh_cyw43_mdns_publish"
        "jh_cyw43_mdns_remove")
    string(FIND
        "${_cyw43_cmake}\n${_rp_native_cmake}\n${_lwipopts}\n${_rp_ota}\n${_cyw43_mdns}\n${_cyw43_driver_wrapper}\n${_mdns_adapter}\n${_mdns_teardown}"
        "${_mdns_contract}" _mdns_contract_at)
    if(_mdns_contract_at EQUAL -1)
        message(FATAL_ERROR
            "CYW43 OTA mDNS contract is missing: ${_mdns_contract}")
    endif()
endforeach()

foreach(_stm32_mdns_contract IN ITEMS
        "HAL_ENABLE_OTA \${_feature_defines}|OTA \"\${_stm32_has_ota}\""
        "HAL_ENABLE_OTA \${_jh_stm32_selection_defines}|OTA \"\${_jh_stm32_has_ota}\"")
    string(REPLACE "|" ";" _stm32_mdns_parts
        "${_stm32_mdns_contract}")
    list(GET _stm32_mdns_parts 0 _stm32_ota_detection)
    list(GET _stm32_mdns_parts 1 _stm32_mdns_selection)
    if(_stm32_mdns_selection MATCHES "_jh_stm32")
        set(_stm32_mdns_cmake "${_stm32_library_cmake}")
    else()
        set(_stm32_mdns_cmake "${_stm32_firmware_cmake}")
    endif()
    foreach(_stm32_mdns_fragment IN ITEMS
            "${_stm32_ota_detection}" "${_stm32_mdns_selection}")
        string(FIND "${_stm32_mdns_cmake}" "${_stm32_mdns_fragment}"
            _stm32_mdns_fragment_at)
        if(_stm32_mdns_fragment_at EQUAL -1)
            message(FATAL_ERROR
                "STM32 CYW43 OTA build does not select mDNS: "
                "${_stm32_mdns_fragment}")
        endif()
    endforeach()
endforeach()

foreach(_mdns_teardown_handler IN ITEMS
        "mdns_probe_and_announce"
        "mdns_multicast_timeout_reset_ipv4"
        "mdns_multicast_probe_timeout_reset_ipv4"
        "mdns_multicast_timeout_25ttl_reset_ipv4"
        "mdns_send_multicast_msg_delayed_ipv4"
        "mdns_send_unicast_msg_delayed_ipv4"
        "mdns_multicast_timeout_reset_ipv6"
        "mdns_multicast_probe_timeout_reset_ipv6"
        "mdns_multicast_timeout_25ttl_reset_ipv6"
        "mdns_send_multicast_msg_delayed_ipv6"
        "mdns_send_unicast_msg_delayed_ipv6"
        "mdns_handle_tc_question"
        "pending_tc_questions")
    string(FIND "${_mdns_teardown}" "${_mdns_teardown_handler}"
        _mdns_teardown_handler_at)
    if(_mdns_teardown_handler_at EQUAL -1)
        message(FATAL_ERROR
            "CYW43 mDNS teardown misses pending work: ${_mdns_teardown_handler}")
    endif()
endforeach()

string(FIND "${_mdns_teardown}"
    "\n  JH_LWIP_MDNS_SET_CLIENT_DATA(netif, NULL);"
    _mdns_detach_at)
string(FIND "${_mdns_teardown}" "\n  JH_LWIP_MDNS_LEAVE_IPV4(netif);"
    _mdns_leave_at)
string(FIND "${_mdns_teardown}" "\n  JH_LWIP_MDNS_MEM_FREE(mdns);"
    _mdns_host_free_at)
if(_mdns_detach_at LESS 0 OR _mdns_leave_at LESS_EQUAL _mdns_detach_at OR
   _mdns_host_free_at LESS_EQUAL _mdns_leave_at)
    message(FATAL_ERROR
        "CYW43 mDNS teardown must detach before multicast leave and free last")
endif()

string(FIND "${_cyw43_driver_wrapper}"
    "jh_cyw43_driver_stop(void)" _driver_stop_begin)
string(FIND "${_cyw43_driver_wrapper}"
    "jh_cyw43_driver_restart" _driver_restart_begin)
string(FIND "${_cyw43_driver_wrapper}"
    "jh_cyw43_driver_is_ready" _driver_restart_end)
if(_driver_stop_begin LESS 0 OR _driver_restart_begin LESS 0 OR
   _driver_restart_end LESS 0 OR
   _driver_restart_begin LESS_EQUAL _driver_stop_begin OR
   _driver_restart_end LESS_EQUAL _driver_restart_begin)
    message(FATAL_ERROR "CYW43 driver lifecycle functions cannot be located")
endif()
math(EXPR _driver_stop_length
    "${_driver_restart_begin} - ${_driver_stop_begin}")
math(EXPR _driver_restart_length
    "${_driver_restart_end} - ${_driver_restart_begin}")
string(SUBSTRING "${_cyw43_driver_wrapper}" ${_driver_stop_begin}
    ${_driver_stop_length} _driver_stop_body)
string(SUBSTRING "${_cyw43_driver_wrapper}" ${_driver_restart_begin}
    ${_driver_restart_length} _driver_restart_body)
foreach(_driver_lifecycle_body IN ITEMS _driver_stop_body _driver_restart_body)
    string(FIND "${${_driver_lifecycle_body}}" "jh_cyw43_mdns_remove"
        _mdns_remove_at)
    string(FIND "${${_driver_lifecycle_body}}" "cyw43_deinit"
        _cyw43_deinit_at)
    if(_mdns_remove_at LESS 0 OR _cyw43_deinit_at LESS 0 OR
       _cyw43_deinit_at LESS_EQUAL _mdns_remove_at)
        message(FATAL_ERROR
            "CYW43 ${_driver_lifecycle_body} must remove mDNS before netif teardown")
    endif()
endforeach()

set(_lwipopts_compact "${_lwipopts}")
foreach(_format_character IN ITEMS " " "\t" "\r" "\n" "\\")
    string(REPLACE "${_format_character}" "" _lwipopts_compact
        "${_lwipopts_compact}")
endforeach()
if(NOT _lwipopts_compact MATCHES
   "MEMP_NUM_SYS_TIMEOUT\\(LWIP_NUM_SYS_TIMEOUT_INTERNAL\\+JH_LWIP_WIREGUARD_SYS_TIMEOUT_RESERVE\\+JH_LWIP_MDNS_SYS_TIMEOUT_RESERVE\\)")
    message(FATAL_ERROR
        "CYW43 lwIP timeout pool must reserve independent WireGuard and full mDNS capacity")
endif()

string(FIND "${_cyw43_driver_wrapper}"
    "jh_cyw43_port_pin_read" _cyw43_pin_read_begin)
string(FIND "${_cyw43_driver_wrapper}"
    "jh_cyw43_port_pin_low" _cyw43_pin_read_end)
if(_cyw43_pin_read_begin LESS 0 OR _cyw43_pin_read_end LESS 0 OR
   _cyw43_pin_read_end LESS _cyw43_pin_read_begin)
    message(FATAL_ERROR
        "CYW43 pin read must refresh polled HOST_WAKE before testing its latch")
endif()
math(EXPR _cyw43_pin_read_length
    "${_cyw43_pin_read_end} - ${_cyw43_pin_read_begin}")
string(SUBSTRING "${_cyw43_driver_wrapper}" ${_cyw43_pin_read_begin}
    ${_cyw43_pin_read_length} _cyw43_pin_read_body)
if(NOT _cyw43_pin_read_body MATCHES
   "jh_cyw43_gspi_host_wake_refresh\\(s_transport\\)")
    message(FATAL_ERROR
        "CYW43 pin read must refresh polled HOST_WAKE before testing its latch")
endif()

set(_manifest "${_driver}/vendor/SHA256SUMS")
if(NOT EXISTS "${_manifest}")
    message(FATAL_ERROR "Missing CYW43 checksum manifest: ${_manifest}")
endif()

file(STRINGS "${_manifest}" _manifest_lines)
set(_pinned_paths)
foreach(_line IN LISTS _manifest_lines)
    if(_line MATCHES "^[ \t]*(#.*)?$")
        continue()
    endif()
    if(NOT _line MATCHES "^([0-9a-f]+)  (.+)$")
        message(FATAL_ERROR "Malformed CYW43 checksum entry: ${_line}")
    endif()
    set(_expected_sha256 "${CMAKE_MATCH_1}")
    set(_relative_path "vendor/${CMAKE_MATCH_2}")
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
if(NOT _pinned_paths)
    message(FATAL_ERROR "CYW43 checksum manifest lists no files")
endif()

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
    "${JH_ROOT}/src/hal/network/cyw43/jh_cyw43_radio.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_platform.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_provider.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_gspi.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/hal_ota.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/hal_tcp.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/hal_time.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/hal_udp.cpp"
    "${JH_ROOT}/src/hal/impl/rp2040/rp2040_lwip_extension_port.cpp"
    "${JH_ROOT}/src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_cyw43_platform.cpp"
    "${JH_ROOT}/src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_cyw43_network_backend.cpp"
    "${JH_ROOT}/src/hal/network/hal_net.cpp"
    "${JH_ROOT}/src/hal/network/hal_tcp.cpp"
    "${JH_ROOT}/src/hal/network/hal_udp.cpp"
    "${JH_ROOT}/src/hal/network/hal_wifi.cpp"
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
        "${JH_ROOT}/src/hal/network/hal_wifi.cpp"
        "${JH_ROOT}/src/hal/network/hal_net.cpp"
        "${JH_ROOT}/src/hal/network/hal_tcp.cpp"
        "${JH_ROOT}/src/hal/network/hal_udp.cpp"
        "${JH_ROOT}/src/hal/network/mqtt/hal_mqtt.cpp"
        "${JH_ROOT}/src/hal/network/wireguard/hal_wireguard.cpp")
    file(READ "${_source}" _contents)
    if(NOT _contents MATCHES "jh_network_require_(hardware|ready)")
        message(FATAL_ERROR
            "Public CYW43 runtime preflight is missing in ${_source}")
    endif()
endforeach()

foreach(_source IN ITEMS
        "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_platform.cpp"
        "${JH_ROOT}/src/hal/impl/rp2040/drivers/rp2040/rp2040_cyw43_provider.cpp"
        "${JH_ROOT}/src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_cyw43_platform.cpp"
        "${JH_ROOT}/src/hal/impl/stm32g474/drivers/stm32g474/stm32g474_cyw43_network_backend.cpp")
    file(READ "${_source}" _contents)
    if(NOT _contents MATCHES
       "hal/network/cyw43/jh_cyw43_driver\\.h")
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
        "PICO_CYW43_SUPPORTED"
        "CYW43_PIN_WL_DYNAMIC"
        "CYW43_PIO_CLOCK_DIV_DYNAMIC")
    if(_rp2040_profiles MATCHES "${_forbidden}")
        message(FATAL_ERROR
            "RP2040 board profiles retain carrier network token '${_forbidden}'")
    endif()
endforeach()
