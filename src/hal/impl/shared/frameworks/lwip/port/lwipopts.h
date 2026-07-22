#ifndef JASZCZURHAL_LWIPOPTS_H
#define JASZCZURHAL_LWIPOPTS_H

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
#define LWIP_TCP 0
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0

#define LWIP_DHCP 1
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0
#define LWIP_DNS 1
#define DNS_TABLE_SIZE 2
#define DNS_MAX_NAME_LENGTH 128
#define LWIP_DNS_SUPPORT_MDNS_QUERIES 0

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

#define LWIP_DEBUG 0
#define LWIP_NOASSERT 0

#endif
