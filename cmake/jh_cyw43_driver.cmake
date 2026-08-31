include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/jh_btstack.cmake")

function(jh_cyw43_source_manifest OUT_SOURCES OUT_INCLUDES)
    cmake_parse_arguments(JH_CYW43 "LWIP;BLUETOOTH;MDNS" "" "" ${ARGN})
    if(JH_CYW43_MDNS AND NOT JH_CYW43_LWIP)
        message(FATAL_ERROR "CYW43 mDNS requires lwIP")
    endif()
    set(_jh_cyw43_root
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/hal/network/cyw43")
    set(_jh_cyw43_vendor "${_jh_cyw43_root}/vendor")
    # The ctrl layer (cyw43_init/cyw43_ensure_up/cyw43_gpio_set) is required for
    # every CYW43 bring-up, including the no-lwIP LED-only path; only the lwIP
    # glue is gated by JH_CYW43_LWIP.
    set(_jh_cyw43_sources
        "${_jh_cyw43_vendor}/src/cyw43_ll.c.upstream"
        "${_jh_cyw43_vendor}/src/cyw43_spi.c.upstream"
        "${_jh_cyw43_vendor}/src/cyw43_ctrl.c.upstream")
    if(JH_CYW43_LWIP)
        list(APPEND _jh_cyw43_sources
            "${_jh_cyw43_vendor}/src/cyw43_lwip.c.upstream")
    endif()
    if(JH_CYW43_BLUETOOTH)
        list(APPEND _jh_cyw43_sources
            "${_jh_cyw43_vendor}/src/cybt_shared_bus.c.upstream"
            "${_jh_cyw43_vendor}/src/cybt_shared_bus_driver.c.upstream")
    endif()

    set(_jh_cyw43_includes
        "${_jh_cyw43_root}"
        "${_jh_cyw43_vendor}"
        "${_jh_cyw43_vendor}/src")
    if(JH_CYW43_BLUETOOTH)
        list(APPEND _jh_cyw43_includes
            "${_jh_cyw43_vendor}/firmware")
    endif()

    if(JH_CYW43_LWIP)
        set(_jh_lwip_port
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/hal/network/lwip/port")
        set(_jh_lwip_root
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/lwip")
        list(APPEND _jh_cyw43_sources
            "${_jh_lwip_root}/src/core/def.c"
            "${_jh_lwip_root}/src/core/dns.c"
            "${_jh_lwip_root}/src/core/inet_chksum.c"
            "${_jh_lwip_root}/src/core/init.c"
            "${_jh_lwip_root}/src/core/ip.c"
            "${_jh_lwip_root}/src/core/mem.c"
            "${_jh_lwip_root}/src/core/memp.c"
            "${_jh_lwip_root}/src/core/netif.c"
            "${_jh_lwip_root}/src/core/pbuf.c"
            "${_jh_lwip_root}/src/core/raw.c"
            "${_jh_lwip_root}/src/core/stats.c"
            "${_jh_lwip_root}/src/core/sys.c"
            "${_jh_lwip_root}/src/core/tcp.c"
            "${_jh_lwip_root}/src/core/tcp_in.c"
            "${_jh_lwip_root}/src/core/tcp_out.c"
            "${_jh_lwip_root}/src/core/timeouts.c"
            "${_jh_lwip_root}/src/core/udp.c"
            "${_jh_lwip_root}/src/core/ipv4/acd.c"
            "${_jh_lwip_root}/src/core/ipv4/autoip.c"
            "${_jh_lwip_root}/src/core/ipv4/dhcp.c"
            "${_jh_lwip_root}/src/core/ipv4/etharp.c"
            "${_jh_lwip_root}/src/core/ipv4/icmp.c"
            "${_jh_lwip_root}/src/core/ipv4/igmp.c"
            "${_jh_lwip_root}/src/core/ipv4/ip4.c"
            "${_jh_lwip_root}/src/core/ipv4/ip4_addr.c"
            "${_jh_lwip_root}/src/core/ipv4/ip4_frag.c"
            "${_jh_lwip_root}/src/netif/ethernet.c")
        if(JH_CYW43_MDNS)
            list(APPEND _jh_cyw43_sources
                "${_jh_lwip_root}/src/apps/mdns/mdns.c"
                "${_jh_lwip_root}/src/apps/mdns/mdns_domain.c"
                "${_jh_lwip_root}/src/apps/mdns/mdns_out.c")
        endif()
        list(APPEND _jh_cyw43_includes
            "${_jh_lwip_port}"
            "${_jh_lwip_root}/src/include")
    endif()

    foreach(_jh_source IN LISTS _jh_cyw43_sources)
        if(NOT EXISTS "${_jh_source}")
            message(FATAL_ERROR
                "Pinned CYW43/lwIP source is missing: ${_jh_source}; run "
                "third_party/update_components.sh")
        endif()
    endforeach()
    set(${OUT_SOURCES} ${_jh_cyw43_sources} PARENT_SCOPE)
    set(${OUT_INCLUDES} ${_jh_cyw43_includes} PARENT_SCOPE)
endfunction()

function(jh_target_enable_cyw43_driver TARGET_NAME)
    cmake_parse_arguments(JH_CYW43 "LWIP;BLUETOOTH;MDNS" "" "" ${ARGN})
    set(_jh_cyw43_options)
    if(JH_CYW43_LWIP)
        list(APPEND _jh_cyw43_options LWIP)
    endif()
    if(JH_CYW43_BLUETOOTH)
        list(APPEND _jh_cyw43_options BLUETOOTH)
    endif()
    if(JH_CYW43_MDNS)
        list(APPEND _jh_cyw43_options MDNS)
    endif()
    jh_cyw43_source_manifest(_jh_cyw43_sources _jh_cyw43_includes
        ${_jh_cyw43_options})
    if(JH_CYW43_MDNS)
        set(_jh_mdns_adapter
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/hal/network/lwip/port/jh_lwip_mdns_adapter.inc")
        set(_jh_mdns_teardown
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/hal/network/lwip/port/jh_lwip_mdns_teardown.inc")
        foreach(_jh_mdns_overlay IN ITEMS
                "${_jh_mdns_adapter}" "${_jh_mdns_teardown}")
            if(NOT EXISTS "${_jh_mdns_overlay}")
                message(FATAL_ERROR
                    "CYW43 mDNS lifecycle overlay is missing: ${_jh_mdns_overlay}")
            endif()
        endforeach()
    endif()
    set(_jh_generated_sources)
    foreach(_jh_source IN LISTS _jh_cyw43_sources)
        get_filename_component(_jh_name "${_jh_source}" NAME)
        string(REGEX REPLACE "\\.upstream$" "" _jh_name "${_jh_name}")
        set(_jh_generated
            "${CMAKE_CURRENT_BINARY_DIR}/jh_cyw43/${TARGET_NAME}/${_jh_name}")
        get_filename_component(_jh_generated_dir "${_jh_generated}" DIRECTORY)
        file(MAKE_DIRECTORY "${_jh_generated_dir}")
        if(JH_CYW43_MDNS AND _jh_name STREQUAL "mdns.c")
            configure_file("${_jh_source}"
                "${_jh_generated_dir}/jh_lwip_mdns_upstream.inc" COPYONLY)
            configure_file("${_jh_mdns_adapter}" "${_jh_generated}" COPYONLY)
            configure_file("${_jh_mdns_teardown}"
                "${_jh_generated_dir}/jh_lwip_mdns_teardown.inc" COPYONLY)
        else()
            configure_file("${_jh_source}" "${_jh_generated}" COPYONLY)
        endif()
        list(APPEND _jh_generated_sources "${_jh_generated}")
        if(JH_CYW43_MDNS AND _jh_name STREQUAL "mdns.c")
            set(_jh_generated_mdns_core "${_jh_generated}")
        endif()
    endforeach()

    target_sources(${TARGET_NAME} PRIVATE ${_jh_generated_sources})
    set_source_files_properties(${_jh_generated_sources} PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter")
    if(_jh_generated_mdns_core AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # mdns.c has deeply nested packet parsing paths. Keep their frames
        # bounded on firmware targets whose cooperative lwIP stack is serviced
        # from the application's fixed-size main stack.
        set_property(SOURCE "${_jh_generated_mdns_core}" APPEND PROPERTY
            COMPILE_OPTIONS -fconserve-stack)
    endif()
    # The pinned JaszczurHAL headers must win over any CYW43/lwIP headers
    # exposed by the surrounding SDK.
    target_include_directories(${TARGET_NAME} BEFORE PRIVATE ${_jh_cyw43_includes})
    if(JH_CYW43_BLUETOOTH)
        target_compile_definitions(${TARGET_NAME} PRIVATE
            JH_CYW43_BLUETOOTH=1)
    endif()
endfunction()

# Apply the feature-derived CYW43 and BTstack modes to a target. Callers retain
# ownership of provider detection and pass resolved boolean inputs here.
function(jh_target_enable_cyw43_feature_stack TARGET_NAME)
    cmake_parse_arguments(JH_CYW43_FEATURE ""
        "LWIP;OTA;BLUETOOTH_STAGE1;BLUETOOTH_CLASSIC_HID;GAMEPAD;BLE;BLE_STREAM" "" ${ARGN})
    if(JH_CYW43_FEATURE_BLUETOOTH_STAGE1 AND
       (JH_CYW43_FEATURE_BLUETOOTH_CLASSIC_HID OR
        JH_CYW43_FEATURE_GAMEPAD OR
        JH_CYW43_FEATURE_BLE OR JH_CYW43_FEATURE_BLE_STREAM))
        message(FATAL_ERROR
            "The private Bluetooth stage-1 probe cannot use a public profile")
    endif()
    if(JH_CYW43_FEATURE_BLUETOOTH_CLASSIC_HID AND
       (JH_CYW43_FEATURE_GAMEPAD OR JH_CYW43_FEATURE_BLE OR
        JH_CYW43_FEATURE_BLE_STREAM))
        message(FATAL_ERROR
            "The private Classic HID probe cannot use a public profile")
    endif()

    set(_jh_cyw43_options)
    if(JH_CYW43_FEATURE_LWIP)
        list(APPEND _jh_cyw43_options LWIP)
    endif()
    if(JH_CYW43_FEATURE_OTA)
        list(APPEND _jh_cyw43_options MDNS)
    endif()
    if(JH_CYW43_FEATURE_BLUETOOTH_STAGE1 OR
       JH_CYW43_FEATURE_BLUETOOTH_CLASSIC_HID OR
       JH_CYW43_FEATURE_GAMEPAD OR
       JH_CYW43_FEATURE_BLE OR JH_CYW43_FEATURE_BLE_STREAM)
        list(APPEND _jh_cyw43_options BLUETOOTH)
    endif()
    jh_target_enable_cyw43_driver(${TARGET_NAME} ${_jh_cyw43_options})

    if(JH_CYW43_FEATURE_BLUETOOTH_STAGE1)
        jh_target_enable_btstack_stage1(${TARGET_NAME})
    elseif(JH_CYW43_FEATURE_BLUETOOTH_CLASSIC_HID)
        jh_target_enable_btstack_classic_hid(${TARGET_NAME})
    elseif(JH_CYW43_FEATURE_GAMEPAD AND JH_CYW43_FEATURE_BLE_STREAM)
        jh_target_enable_btstack_gamepad_ble_stream(${TARGET_NAME})
    elseif(JH_CYW43_FEATURE_GAMEPAD AND JH_CYW43_FEATURE_BLE)
        jh_target_enable_btstack_gamepad_ble(${TARGET_NAME})
    elseif(JH_CYW43_FEATURE_GAMEPAD)
        jh_target_enable_btstack_gamepad(${TARGET_NAME})
    elseif(JH_CYW43_FEATURE_BLE_STREAM)
        jh_target_enable_btstack_ble_stream(${TARGET_NAME})
    elseif(JH_CYW43_FEATURE_BLE)
        jh_target_enable_btstack_ble(${TARGET_NAME})
    endif()
endfunction()
