/*
 * This fragment is included after the pinned lwIP mdns.c implementation. It
 * intentionally shares that translation unit so it can drain the private
 * pending_tc_questions list before releasing the per-netif mdns_host.
 */

#ifndef JH_LWIP_MDNS_UNTIMEOUT
#define JH_LWIP_MDNS_UNTIMEOUT(handler, argument)                              \
  sys_untimeout((handler), (argument))
#endif

#ifndef JH_LWIP_MDNS_PBUF_FREE
#define JH_LWIP_MDNS_PBUF_FREE(packet_buffer) pbuf_free(packet_buffer)
#endif

#ifndef JH_LWIP_MDNS_PACKET_FREE
#define JH_LWIP_MDNS_PACKET_FREE(packet) LWIP_MEMPOOL_FREE(MDNS_PKTS, packet)
#endif

#ifndef JH_LWIP_MDNS_SET_CLIENT_DATA
#define JH_LWIP_MDNS_SET_CLIENT_DATA(netif, data)                              \
  netif_set_client_data((netif), mdns_netif_client_id, (data))
#endif

#ifndef JH_LWIP_MDNS_LEAVE_IPV4
#define JH_LWIP_MDNS_LEAVE_IPV4(netif)                                         \
  igmp_leavegroup_netif((netif), ip_2_ip4(&v4group))
#endif

#ifndef JH_LWIP_MDNS_LEAVE_IPV6
#define JH_LWIP_MDNS_LEAVE_IPV6(netif)                                         \
  mld6_leavegroup_netif((netif), ip_2_ip6(&v6group))
#endif

#ifndef JH_LWIP_MDNS_MEM_FREE
#define JH_LWIP_MDNS_MEM_FREE(memory) mem_free(memory)
#endif

static void jh_lwip_mdns_free_pending_packet(struct mdns_packet *packet) {
  while (packet->next_answer != NULL) {
    struct mdns_packet *answer = packet->next_answer;
    packet->next_answer = answer->next_answer;
    JH_LWIP_MDNS_PBUF_FREE(answer->pbuf);
    JH_LWIP_MDNS_PACKET_FREE(answer);
  }
  JH_LWIP_MDNS_PBUF_FREE(packet->pbuf);
  JH_LWIP_MDNS_PACKET_FREE(packet);
}

static void jh_lwip_mdns_cancel_pending_questions(struct netif *netif) {
  struct mdns_packet **cursor = &pending_tc_questions;
  const u8_t netif_index = netif_get_index(netif);

  while (*cursor != NULL) {
    struct mdns_packet *packet = *cursor;
    if ((packet->pbuf != NULL) && (packet->pbuf->if_idx == netif_index)) {
      JH_LWIP_MDNS_UNTIMEOUT(mdns_handle_tc_question, packet);
      *cursor = packet->next_tc_question;
      jh_lwip_mdns_free_pending_packet(packet);
    } else {
      cursor = &packet->next_tc_question;
    }
  }
}

static void jh_lwip_mdns_cancel_netif_timeouts(struct netif *netif) {
  JH_LWIP_MDNS_UNTIMEOUT(mdns_probe_and_announce, netif);
#if LWIP_IPV4
  JH_LWIP_MDNS_UNTIMEOUT(mdns_multicast_timeout_reset_ipv4, netif);
  JH_LWIP_MDNS_UNTIMEOUT(mdns_multicast_probe_timeout_reset_ipv4, netif);
  JH_LWIP_MDNS_UNTIMEOUT(mdns_multicast_timeout_25ttl_reset_ipv4, netif);
  JH_LWIP_MDNS_UNTIMEOUT(mdns_send_multicast_msg_delayed_ipv4, netif);
  JH_LWIP_MDNS_UNTIMEOUT(mdns_send_unicast_msg_delayed_ipv4, netif);
#endif
#if LWIP_IPV6
  JH_LWIP_MDNS_UNTIMEOUT(mdns_multicast_timeout_reset_ipv6, netif);
  JH_LWIP_MDNS_UNTIMEOUT(mdns_multicast_probe_timeout_reset_ipv6, netif);
  JH_LWIP_MDNS_UNTIMEOUT(mdns_multicast_timeout_25ttl_reset_ipv6, netif);
  JH_LWIP_MDNS_UNTIMEOUT(mdns_send_multicast_msg_delayed_ipv6, netif);
  JH_LWIP_MDNS_UNTIMEOUT(mdns_send_unicast_msg_delayed_ipv6, netif);
#endif
  jh_lwip_mdns_cancel_pending_questions(netif);
}

err_t mdns_resp_remove_netif(struct netif *netif) {
  int i;
  struct mdns_host *mdns;

  LWIP_ASSERT_CORE_LOCKED();
  LWIP_ASSERT("mdns_resp_remove_netif: Null pointer", netif);
  mdns = NETIF_TO_HOST(netif);
  LWIP_ERROR("mdns_resp_remove_netif: Not an active netif", (mdns != NULL),
             return ERR_VAL);

  /*
   * Detach first. CYW43 multicast-filter updates can synchronously deliver an
   * Ethernet frame while leaving the group. The responder must reject that
   * frame instead of arming fresh work against state being destroyed.
   */
  JH_LWIP_MDNS_SET_CLIENT_DATA(netif, NULL);
  jh_lwip_mdns_cancel_netif_timeouts(netif);

#if LWIP_IPV4
  JH_LWIP_MDNS_LEAVE_IPV4(netif);
#endif
#if LWIP_IPV6
  JH_LWIP_MDNS_LEAVE_IPV6(netif);
#endif

  for (i = 0; i < MDNS_MAX_SERVICES; i++) {
    struct mdns_service *service = mdns->services[i];
    if (service != NULL) {
      JH_LWIP_MDNS_MEM_FREE(service);
    }
  }
  JH_LWIP_MDNS_MEM_FREE(mdns);
  return ERR_OK;
}

#undef JH_LWIP_MDNS_UNTIMEOUT
#undef JH_LWIP_MDNS_PBUF_FREE
#undef JH_LWIP_MDNS_PACKET_FREE
#undef JH_LWIP_MDNS_SET_CLIENT_DATA
#undef JH_LWIP_MDNS_LEAVE_IPV4
#undef JH_LWIP_MDNS_LEAVE_IPV6
#undef JH_LWIP_MDNS_MEM_FREE
