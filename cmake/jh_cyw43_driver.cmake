include_guard(GLOBAL)

function(jh_cyw43_source_manifest OUT_SOURCES OUT_INCLUDES)
    cmake_parse_arguments(JH_CYW43 "LWIP;BLUETOOTH" "" "" ${ARGN})
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
    cmake_parse_arguments(JH_CYW43 "LWIP;BLUETOOTH" "" "" ${ARGN})
    set(_jh_cyw43_options)
    if(JH_CYW43_LWIP)
        list(APPEND _jh_cyw43_options LWIP)
    endif()
    if(JH_CYW43_BLUETOOTH)
        list(APPEND _jh_cyw43_options BLUETOOTH)
    endif()
    jh_cyw43_source_manifest(_jh_cyw43_sources _jh_cyw43_includes
        ${_jh_cyw43_options})
    set(_jh_generated_sources)
    foreach(_jh_source IN LISTS _jh_cyw43_sources)
        get_filename_component(_jh_name "${_jh_source}" NAME)
        string(REGEX REPLACE "\\.upstream$" "" _jh_name "${_jh_name}")
        set(_jh_generated
            "${CMAKE_CURRENT_BINARY_DIR}/jh_cyw43/${TARGET_NAME}/${_jh_name}")
        get_filename_component(_jh_generated_dir "${_jh_generated}" DIRECTORY)
        file(MAKE_DIRECTORY "${_jh_generated_dir}")
        configure_file("${_jh_source}" "${_jh_generated}" COPYONLY)
        list(APPEND _jh_generated_sources "${_jh_generated}")
    endforeach()

    target_sources(${TARGET_NAME} PRIVATE ${_jh_generated_sources})
    set_source_files_properties(${_jh_generated_sources} PROPERTIES
        COMPILE_OPTIONS "-Wno-unused-parameter")
    # The pinned JaszczurHAL headers must win over any CYW43/lwIP headers
    # exposed by the surrounding SDK.
    target_include_directories(${TARGET_NAME} BEFORE PRIVATE ${_jh_cyw43_includes})
    if(JH_CYW43_BLUETOOTH)
        target_compile_definitions(${TARGET_NAME} PRIVATE
            JH_CYW43_BLUETOOTH=1)
    endif()
endfunction()
