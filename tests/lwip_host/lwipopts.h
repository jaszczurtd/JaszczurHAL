#ifndef JH_TESTS_LWIP_HOST_LWIPOPTS_H
#define JH_TESTS_LWIP_HOST_LWIPOPTS_H

/*
 * Real lwIP core for host tests of the WireGuard engine: NO_SYS, IPv4 and UDP
 * only, the same 12 KB heap as the HAL port (src/hal/network/lwip/port) and
 * statistics on, so a test can read lwip_stats.mem and the memp pools.
 */
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
#define LWIP_ETHERNET 0
#define LWIP_ARP 0
#define LWIP_ICMP 0
#define LWIP_RAW 0
#define LWIP_IGMP 0
#define LWIP_UDP 1
#define MEMP_NUM_UDP_PCB 4
#define LWIP_TCP 0
#define LWIP_DHCP 0
#define LWIP_AUTOIP 0
#define LWIP_DNS 0
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define IP_REASSEMBLY 0
#define IP_FRAG 0

#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 1)

#define LWIP_STATS 1
#define MEM_STATS 1
#define MEMP_STATS 1
#define SYS_STATS 0
#define LWIP_STATS_DISPLAY 0

#endif
