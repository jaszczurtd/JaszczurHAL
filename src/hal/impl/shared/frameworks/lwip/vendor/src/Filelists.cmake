# This file is indended to be included in end-user CMakeLists.txt
# include(/path/to/Filelists.cmake)
# It assumes the variable LWIP_DIR is defined pointing to the
# root path of lwIP sources.
#
# This file is NOT designed (on purpose) to be used as cmake
# subdir via add_subdirectory()
# The intention is to provide greater flexibility to users to
# create their own targets using the *_SRCS variables.

if(NOT ${CMAKE_VERSION} VERSION_LESS "3.10.0")
    include_guard(GLOBAL)
endif()

set(LWIP_VERSION_MAJOR    "2")
set(LWIP_VERSION_MINOR    "2")
set(LWIP_VERSION_REVISION "1")
# LWIP_VERSION_RC is set to LWIP_RC_RELEASE for official releases
# LWIP_VERSION_RC is set to LWIP_RC_DEVELOPMENT for Git versions
# Numbers 1..31 are reserved for release candidates
set(LWIP_VERSION_RC       "LWIP_RC_RELEASE")

if ("${LWIP_VERSION_RC}" STREQUAL "LWIP_RC_RELEASE")
    set(LWIP_VERSION_STRING
        "${LWIP_VERSION_MAJOR}.${LWIP_VERSION_MINOR}.${LWIP_VERSION_REVISION}"
    )
elseif ("${LWIP_VERSION_RC}" STREQUAL "LWIP_RC_DEVELOPMENT")
    set(LWIP_VERSION_STRING
        "${LWIP_VERSION_MAJOR}.${LWIP_VERSION_MINOR}.${LWIP_VERSION_REVISION}.dev"
    )
else()
    set(LWIP_VERSION_STRING
        "${LWIP_VERSION_MAJOR}.${LWIP_VERSION_MINOR}.${LWIP_VERSION_REVISION}.rc${LWIP_VERSION_RC}"
    )
endif()

# The minimum set of files needed for lwIP.
set(lwipcore_SRCS
    ${LWIP_DIR}/src/core/init.c.upstream
    ${LWIP_DIR}/src/core/def.c.upstream
    ${LWIP_DIR}/src/core/dns.c.upstream
    ${LWIP_DIR}/src/core/inet_chksum.c.upstream
    ${LWIP_DIR}/src/core/ip.c.upstream
    ${LWIP_DIR}/src/core/mem.c.upstream
    ${LWIP_DIR}/src/core/memp.c.upstream
    ${LWIP_DIR}/src/core/netif.c.upstream
    ${LWIP_DIR}/src/core/pbuf.c.upstream
    ${LWIP_DIR}/src/core/raw.c.upstream
    ${LWIP_DIR}/src/core/stats.c.upstream
    ${LWIP_DIR}/src/core/sys.c.upstream
    ${LWIP_DIR}/src/core/altcp.c.upstream
    ${LWIP_DIR}/src/core/altcp_alloc.c.upstream
    ${LWIP_DIR}/src/core/altcp_tcp.c.upstream
    ${LWIP_DIR}/src/core/tcp.c.upstream
    ${LWIP_DIR}/src/core/tcp_in.c.upstream
    ${LWIP_DIR}/src/core/tcp_out.c.upstream
    ${LWIP_DIR}/src/core/timeouts.c.upstream
    ${LWIP_DIR}/src/core/udp.c.upstream
)
set(lwipcore4_SRCS
    ${LWIP_DIR}/src/core/ipv4/acd.c.upstream
    ${LWIP_DIR}/src/core/ipv4/autoip.c.upstream
    ${LWIP_DIR}/src/core/ipv4/dhcp.c.upstream
    ${LWIP_DIR}/src/core/ipv4/etharp.c.upstream
    ${LWIP_DIR}/src/core/ipv4/icmp.c.upstream
    ${LWIP_DIR}/src/core/ipv4/igmp.c.upstream
    ${LWIP_DIR}/src/core/ipv4/ip4_frag.c.upstream
    ${LWIP_DIR}/src/core/ipv4/ip4.c.upstream
    ${LWIP_DIR}/src/core/ipv4/ip4_addr.c.upstream
)
set(lwipcore6_SRCS
    ${LWIP_DIR}/src/core/ipv6/dhcp6.c.upstream
    ${LWIP_DIR}/src/core/ipv6/ethip6.c.upstream
    ${LWIP_DIR}/src/core/ipv6/icmp6.c.upstream
    ${LWIP_DIR}/src/core/ipv6/inet6.c.upstream
    ${LWIP_DIR}/src/core/ipv6/ip6.c.upstream
    ${LWIP_DIR}/src/core/ipv6/ip6_addr.c.upstream
    ${LWIP_DIR}/src/core/ipv6/ip6_frag.c.upstream
    ${LWIP_DIR}/src/core/ipv6/mld6.c.upstream
    ${LWIP_DIR}/src/core/ipv6/nd6.c.upstream
)

# APIFILES: The files which implement the sequential and socket APIs.
set(lwipapi_SRCS
    ${LWIP_DIR}/src/api/api_lib.c.upstream
    ${LWIP_DIR}/src/api/api_msg.c.upstream
    ${LWIP_DIR}/src/api/err.c.upstream
    ${LWIP_DIR}/src/api/if_api.c.upstream
    ${LWIP_DIR}/src/api/netbuf.c.upstream
    ${LWIP_DIR}/src/api/netdb.c.upstream
    ${LWIP_DIR}/src/api/netifapi.c.upstream
    ${LWIP_DIR}/src/api/sockets.c.upstream
    ${LWIP_DIR}/src/api/tcpip.c.upstream
)

# Files implementing various generic network interface functions
set(lwipnetif_SRCS
    ${LWIP_DIR}/src/netif/ethernet.c.upstream
    ${LWIP_DIR}/src/netif/bridgeif.c.upstream
    ${LWIP_DIR}/src/netif/bridgeif_fdb.c.upstream
)

if (NOT ${LWIP_EXCLUDE_SLIPIF})
	list(APPEND lwipnetif_SRCS ${LWIP_DIR}/src/netif/slipif.c.upstream)
endif()

# 6LoWPAN
set(lwipsixlowpan_SRCS
    ${LWIP_DIR}/src/netif/lowpan6_common.c.upstream
    ${LWIP_DIR}/src/netif/lowpan6.c.upstream
    ${LWIP_DIR}/src/netif/lowpan6_ble.c.upstream
    ${LWIP_DIR}/src/netif/zepif.c.upstream
)

# PPP
set(lwipppp_SRCS
    ${LWIP_DIR}/src/netif/ppp/auth.c.upstream
    ${LWIP_DIR}/src/netif/ppp/ccp.c.upstream
    ${LWIP_DIR}/src/netif/ppp/chap-md5.c.upstream
    ${LWIP_DIR}/src/netif/ppp/chap_ms.c.upstream
    ${LWIP_DIR}/src/netif/ppp/chap-new.c.upstream
    ${LWIP_DIR}/src/netif/ppp/demand.c.upstream
    ${LWIP_DIR}/src/netif/ppp/eap.c.upstream
    ${LWIP_DIR}/src/netif/ppp/ecp.c.upstream
    ${LWIP_DIR}/src/netif/ppp/eui64.c.upstream
    ${LWIP_DIR}/src/netif/ppp/fsm.c.upstream
    ${LWIP_DIR}/src/netif/ppp/ipcp.c.upstream
    ${LWIP_DIR}/src/netif/ppp/ipv6cp.c.upstream
    ${LWIP_DIR}/src/netif/ppp/lcp.c.upstream
    ${LWIP_DIR}/src/netif/ppp/magic.c.upstream
    ${LWIP_DIR}/src/netif/ppp/mppe.c.upstream
    ${LWIP_DIR}/src/netif/ppp/multilink.c.upstream
    ${LWIP_DIR}/src/netif/ppp/ppp.c.upstream
    ${LWIP_DIR}/src/netif/ppp/pppapi.c.upstream
    ${LWIP_DIR}/src/netif/ppp/pppcrypt.c.upstream
    ${LWIP_DIR}/src/netif/ppp/pppoe.c.upstream
    ${LWIP_DIR}/src/netif/ppp/pppol2tp.c.upstream
    ${LWIP_DIR}/src/netif/ppp/pppos.c.upstream
    ${LWIP_DIR}/src/netif/ppp/upap.c.upstream
    ${LWIP_DIR}/src/netif/ppp/utils.c.upstream
    ${LWIP_DIR}/src/netif/ppp/vj.c.upstream
    ${LWIP_DIR}/src/netif/ppp/polarssl/arc4.c.upstream
    ${LWIP_DIR}/src/netif/ppp/polarssl/des.c.upstream
    ${LWIP_DIR}/src/netif/ppp/polarssl/md4.c.upstream
    ${LWIP_DIR}/src/netif/ppp/polarssl/md5.c.upstream
    ${LWIP_DIR}/src/netif/ppp/polarssl/sha1.c.upstream
)

# SNMPv3 agent
set(lwipsnmp_SRCS
    ${LWIP_DIR}/src/apps/snmp/snmp_asn1.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_core.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_mib2.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_mib2_icmp.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_mib2_interfaces.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_mib2_ip.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_mib2_snmp.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_mib2_system.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_mib2_tcp.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_mib2_udp.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_snmpv2_framework.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_snmpv2_usm.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_msg.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmpv3.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_netconn.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_pbuf_stream.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_raw.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_scalar.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_table.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_threadsync.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmp_traps.c.upstream
)

# HTTP server + client
set(lwiphttp_SRCS
    ${LWIP_DIR}/src/apps/http/altcp_proxyconnect.c.upstream
    ${LWIP_DIR}/src/apps/http/fs.c.upstream
    ${LWIP_DIR}/src/apps/http/http_client.c.upstream
    ${LWIP_DIR}/src/apps/http/httpd.c.upstream
)

# MAKEFSDATA HTTP server host utility
set(lwipmakefsdata_SRCS
    ${LWIP_DIR}/src/apps/http/makefsdata/makefsdata.c.upstream
)

# IPERF server
set(lwipiperf_SRCS
    ${LWIP_DIR}/src/apps/lwiperf/lwiperf.c.upstream
)

# SMTP client
set(lwipsmtp_SRCS
    ${LWIP_DIR}/src/apps/smtp/smtp.c.upstream
)

# SNTP client
set(lwipsntp_SRCS
    ${LWIP_DIR}/src/apps/sntp/sntp.c.upstream
)

# MDNS responder
set(lwipmdns_SRCS
    ${LWIP_DIR}/src/apps/mdns/mdns.c.upstream
    ${LWIP_DIR}/src/apps/mdns/mdns_out.c.upstream
    ${LWIP_DIR}/src/apps/mdns/mdns_domain.c.upstream
)

# NetBIOS name server
set(lwipnetbios_SRCS
    ${LWIP_DIR}/src/apps/netbiosns/netbiosns.c.upstream
)

# TFTP server files
set(lwiptftp_SRCS
    ${LWIP_DIR}/src/apps/tftp/tftp.c.upstream
)

# MQTT client files
set(lwipmqtt_SRCS
    ${LWIP_DIR}/src/apps/mqtt/mqtt.c.upstream
)

# ARM MBEDTLS related files of lwIP rep
set(lwipmbedtls_SRCS
    ${LWIP_DIR}/src/apps/altcp_tls/altcp_tls_mbedtls.c.upstream
    ${LWIP_DIR}/src/apps/altcp_tls/altcp_tls_mbedtls_mem.c.upstream
    ${LWIP_DIR}/src/apps/snmp/snmpv3_mbedtls.c.upstream
)

# All LWIP files without apps
set(lwipnoapps_SRCS
    ${lwipcore_SRCS}
    ${lwipcore4_SRCS}
    ${lwipcore6_SRCS}
    ${lwipapi_SRCS}
    ${lwipnetif_SRCS}
    ${lwipsixlowpan_SRCS}
    ${lwipppp_SRCS}
)

# LWIPAPPFILES: All LWIP APPs
set(lwipallapps_SRCS
    ${lwipsnmp_SRCS}
    ${lwiphttp_SRCS}
    ${lwipiperf_SRCS}
    ${lwipsmtp_SRCS}
    ${lwipsntp_SRCS}
    ${lwipmdns_SRCS}
    ${lwipnetbios_SRCS}
    ${lwiptftp_SRCS}
    ${lwipmqtt_SRCS}
)

# Generate lwip/init.h (version info)
configure_file(${LWIP_DIR}/src/include/lwip/init.h.cmake.in ${LWIP_DIR}/src/include/lwip/init.h)

# Documentation
set(DOXYGEN_DIR ${LWIP_DIR}/doc/doxygen)
set(DOXYGEN_OUTPUT_DIR output)
set(DOXYGEN_IN  ${LWIP_DIR}/doc/doxygen/lwip.Doxyfile.cmake.in)
set(DOXYGEN_OUT ${LWIP_DIR}/doc/doxygen/lwip.Doxyfile)
configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT})

find_package(Doxygen)
if (DOXYGEN_FOUND)
    message(STATUS "Doxygen build started")

    add_custom_target(lwipdocs
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${DOXYGEN_DIR}/${DOXYGEN_OUTPUT_DIR}/html
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${DOXYGEN_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)
else (DOXYGEN_FOUND)
    message(STATUS "Doxygen needs to be installed to generate the doxygen documentation")
endif (DOXYGEN_FOUND)

# lwIP libraries
add_library(lwipcore EXCLUDE_FROM_ALL ${lwipnoapps_SRCS})
target_compile_options(lwipcore PRIVATE ${LWIP_COMPILER_FLAGS})
target_compile_definitions(lwipcore PRIVATE ${LWIP_DEFINITIONS}  ${LWIP_MBEDTLS_DEFINITIONS})
target_include_directories(lwipcore PRIVATE ${LWIP_INCLUDE_DIRS} ${LWIP_MBEDTLS_INCLUDE_DIRS})

add_library(lwipallapps EXCLUDE_FROM_ALL ${lwipallapps_SRCS})
target_compile_options(lwipallapps PRIVATE ${LWIP_COMPILER_FLAGS})
target_compile_definitions(lwipallapps PRIVATE ${LWIP_DEFINITIONS}  ${LWIP_MBEDTLS_DEFINITIONS})
target_include_directories(lwipallapps PRIVATE ${LWIP_INCLUDE_DIRS} ${LWIP_MBEDTLS_INCLUDE_DIRS})

add_library(lwipmbedtls EXCLUDE_FROM_ALL ${lwipmbedtls_SRCS})
target_compile_options(lwipmbedtls PRIVATE ${LWIP_COMPILER_FLAGS})
target_compile_definitions(lwipmbedtls PRIVATE ${LWIP_DEFINITIONS}  ${LWIP_MBEDTLS_DEFINITIONS})
target_include_directories(lwipmbedtls PRIVATE ${LWIP_INCLUDE_DIRS} ${LWIP_MBEDTLS_INCLUDE_DIRS})
