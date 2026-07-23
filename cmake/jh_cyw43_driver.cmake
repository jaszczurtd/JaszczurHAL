include_guard(GLOBAL)

function(jh_cyw43_source_manifest OUT_SOURCES OUT_INCLUDES)
    cmake_parse_arguments(JH_CYW43 "LWIP" "" "" ${ARGN})
    set(_jh_cyw43_root
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/hal/impl/shared/drivers/cyw43-driver")
    set(_jh_cyw43_vendor "${_jh_cyw43_root}/vendor")
    set(_jh_cyw43_sources
        "${_jh_cyw43_vendor}/src/cyw43_ll.c.upstream"
        "${_jh_cyw43_vendor}/src/cyw43_spi.c.upstream")
    if(JH_CYW43_LWIP)
        list(APPEND _jh_cyw43_sources
            "${_jh_cyw43_vendor}/src/cyw43_ctrl.c.upstream"
            "${_jh_cyw43_vendor}/src/cyw43_lwip.c.upstream")
    endif()

    set(_jh_cyw43_includes
        "${_jh_cyw43_root}"
        "${_jh_cyw43_vendor}"
        "${_jh_cyw43_vendor}/src")

    if(JH_CYW43_LWIP)
        set(_jh_lwip_root
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/hal/impl/shared/frameworks/lwip")
        list(APPEND _jh_cyw43_sources
            "${_jh_lwip_root}/vendor/src/core/def.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/dns.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/inet_chksum.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/init.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/ip.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/mem.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/memp.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/netif.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/pbuf.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/raw.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/stats.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/sys.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/tcp.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/tcp_in.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/tcp_out.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/timeouts.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/udp.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/ipv4/acd.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/ipv4/autoip.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/ipv4/dhcp.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/ipv4/etharp.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/ipv4/icmp.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/ipv4/igmp.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/ipv4/ip4.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/ipv4/ip4_addr.c.upstream"
            "${_jh_lwip_root}/vendor/src/core/ipv4/ip4_frag.c.upstream"
            "${_jh_lwip_root}/vendor/src/netif/ethernet.c.upstream")
        list(APPEND _jh_cyw43_includes
            "${_jh_lwip_root}/port"
            "${_jh_lwip_root}/vendor/src/include")
    endif()

    foreach(_jh_source IN LISTS _jh_cyw43_sources)
        if(NOT EXISTS "${_jh_source}")
            message(FATAL_ERROR "Pinned CYW43/lwIP source is missing: ${_jh_source}")
        endif()
    endforeach()
    set(${OUT_SOURCES} ${_jh_cyw43_sources} PARENT_SCOPE)
    set(${OUT_INCLUDES} ${_jh_cyw43_includes} PARENT_SCOPE)
endfunction()

function(jh_target_enable_cyw43_driver TARGET_NAME)
    cmake_parse_arguments(JH_CYW43 "LWIP" "" "" ${ARGN})
    if(JH_CYW43_LWIP)
        jh_cyw43_source_manifest(_jh_cyw43_sources _jh_cyw43_includes LWIP)
    else()
        jh_cyw43_source_manifest(_jh_cyw43_sources _jh_cyw43_includes)
    endif()
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
    # exposed by the surrounding SDK or Arduino core.
    target_include_directories(${TARGET_NAME} BEFORE PRIVATE ${_jh_cyw43_includes})
endfunction()
