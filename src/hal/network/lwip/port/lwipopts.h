#ifndef JASZCZURHAL_LWIPOPTS_H
#define JASZCZURHAL_LWIPOPTS_H

#include "hal/core/hal_config.h"

/* Point 23 starts with one cooperatively serviced, IPv4-only CYW43 netif. */
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_TIMERS 1

#define MEM_ALIGNMENT 4
#define MEM_LIBC_MALLOC 0
#define MEM_SIZE (12u * 1024u)
#define MEMP_MEM_MALLOC 0
#define MEMP_NUM_PBUF 8
#define PBUF_POOL_SIZE 8
#define PBUF_POOL_BUFSIZE 1536

#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ETHERNET 1
#define LWIP_ARP 1
#define ARP_QUEUEING 1
#define MEMP_NUM_ARP_QUEUE 4
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define MEMP_NUM_RAW_PCB 2
#define LWIP_IGMP 1

#define LWIP_UDP 1
#define MEMP_NUM_UDP_PCB 6
#if defined(HAL_ENABLE_TCP)
#define LWIP_TCP 1
#define TCP_MSS 1460
#define TCP_WND (4u * TCP_MSS)
#define TCP_SND_BUF (4u * TCP_MSS)
#define TCP_SND_QUEUELEN 16
#define MEMP_NUM_TCP_PCB                                                       \
  (HAL_TCP_SOCKET_MAX_INSTANCES + HAL_TCP_LISTENER_BACKLOG_MAX)
#define MEMP_NUM_TCP_PCB_LISTEN HAL_TCP_LISTENER_MAX_INSTANCES
#define MEMP_NUM_TCP_SEG 16
#else
#define LWIP_TCP 0
#endif
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0

#define LWIP_DHCP 1
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_DNS 1
#define DNS_TABLE_SIZE 2
#define DNS_MAX_NAME_LENGTH 128
#define LWIP_DNS_SUPPORT_MDNS_QUERIES 0

/*
 * The upstream default sizes this pool for lwIP's internal cyclic timers
 * only.  A live WireGuard netif owns one additional periodic timeout and
 * releases it in wireguardif_shutdown().
 */
#if defined(HAL_ENABLE_WIREGUARD)
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 1)
#endif

#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1

#define LWIP_STATS 1
#define MEM_STATS 1
#define MEMP_STATS 1
#define LINK_STATS 1
#define ETHARP_STATS 1
#define IP_STATS 1
#define ICMP_STATS 1
#define UDP_STATS 1
#define SYS_STATS 0
#define LWIP_STATS_DISPLAY 0

#endif
