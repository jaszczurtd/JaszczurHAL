#include "../../../../hal_target.h"

#include "jh_cyw43_lwip.h"

#include <string.h>

#if ((HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                  \
     (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))) &&        \
    defined(HAL_CYW43_STACK_LWIP)

#include "../../network/jh_icmp_echo.h"
#include "jh_cyw43_driver.h"

#include <hal/hal_system.h>

extern "C" {
#include "lwip/dns.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/memp.h"
#include "lwip/raw.h"
#include "lwip/stats.h"
#include "lwip/timeouts.h"
}

namespace {

constexpr size_t kDnsSlotCount = 4u;
constexpr uint16_t kPingIdentifier = 0x4a48u;
constexpr uint16_t kPingPayloadSize = 32u;

struct dns_slot_t {
  bool occupied;
  bool completed;
  bool found;
  uint32_t address;
};

struct ping_request_t {
  uint16_t sequence;
  bool completed;
  int ttl;
};

dns_slot_t s_dns_slots[kDnsSlotCount]{};
uint32_t s_service_calls;
uint32_t s_host_wake_services;
uint16_t s_ping_sequence;
uint32_t s_rand_state;
int32_t s_last_cyw43_error;
bool s_in_service;

hal_status_t status_from_lwip(err_t status) {
  if (status == ERR_OK) {
    return HAL_OK;
  }
  if (status == ERR_MEM || status == ERR_BUF) {
    return HAL_ENOMEM;
  }
  if (status == ERR_TIMEOUT) {
    return HAL_ETIMEOUT;
  }
  if (status == ERR_ARG || status == ERR_VAL) {
    return HAL_EINVAL;
  }
  if (status == ERR_INPROGRESS || status == ERR_WOULDBLOCK) {
    return HAL_EAGAIN;
  }
  return HAL_EIO;
}

hal_status_t status_from_cyw43(int status) {
  if (status == 0) {
    return HAL_OK;
  }
  if (status == -CYW43_ETIMEDOUT) {
    return HAL_ETIMEOUT;
  }
  if (status == -CYW43_EINVAL) {
    return HAL_EINVAL;
  }
  return HAL_EIO;
}

bool deadline_expired(uint32_t start_ms, uint32_t timeout_ms) {
  return (uint32_t)(hal_millis() - start_ms) >= timeout_ms;
}

void dns_found(const char *, const ip_addr_t *address, void *argument) {
  auto *slot = static_cast<dns_slot_t *>(argument);
  if (slot == nullptr || !slot->occupied) {
    return;
  }
  slot->found = address != nullptr && IP_IS_V4(address);
  slot->address = slot->found ? ip_2_ip4(address)->addr : 0u;
  slot->completed = true;
}

uint8_t ping_received(void *argument, struct raw_pcb *, struct pbuf *packet,
                      const ip_addr_t *) {
  auto *request = static_cast<ping_request_t *>(argument);
  uint8_t reply[68]{};
  const size_t reply_size =
      packet->tot_len < sizeof(reply) ? packet->tot_len : sizeof(reply);
  if (pbuf_copy_partial(packet, reply, reply_size, 0u) != reply_size) {
    return 0u;
  }
  int ttl = -1;
  if (jh_icmp_echo_reply_parse(reply, reply_size, kPingIdentifier,
                               request->sequence, &ttl) != HAL_OK) {
    return 0u;
  }
  request->ttl = ttl;
  request->completed = true;
  pbuf_free(packet);
  return 1u;
}

bool netif_is_owned(void) {
  struct netif *cursor;
  NETIF_FOREACH(cursor) {
    if (cursor == &cyw43_state.netif[CYW43_ITF_STA]) {
      return true;
    }
  }
  return false;
}

} // namespace

extern "C" uint32_t jh_lwip_port_rand(void) {
  if (s_rand_state == 0u) {
    uint8_t uid[HAL_DEVICE_UID_BYTES]{};
    (void)hal_get_device_uid(uid);
    s_rand_state = hal_micros() ^ 0x6d2b79f5u;
    for (size_t index = 0u; index < sizeof(uid); ++index) {
      s_rand_state = (s_rand_state << 5u) ^ (s_rand_state >> 2u) ^ uid[index];
    }
    if (s_rand_state == 0u) {
      s_rand_state = 1u;
    }
  }
  uint32_t value = s_rand_state;
  value ^= value << 13u;
  value ^= value >> 17u;
  value ^= value << 5u;
  s_rand_state = value;
  return value;
}

extern "C" uint32_t sys_now(void) { return hal_millis(); }

extern "C" __attribute__((noreturn)) void
jh_lwip_port_assert(const char *, const char *, int) {
  __builtin_trap();
  for (;;) {
  }
}

extern "C" hal_status_t jh_cyw43_lwip_service(void) {
  if (!jh_cyw43_driver_is_ready()) {
    return HAL_EUNINIT;
  }
  if (s_in_service) {
    return HAL_EBUSY;
  }
  s_in_service = true;
  jh_cyw43_gspi_transport_t *transport = jh_cyw43_driver_transport_internal();
  hal_status_t status = jh_cyw43_gspi_host_wake_refresh(transport);
  if (status != HAL_OK) {
    s_in_service = false;
    return status;
  }
  const bool host_wake = jh_cyw43_gspi_host_wake_pending(transport);
  if (cyw43_poll != nullptr) {
    cyw43_poll();
  }
  sys_check_timeouts();
  if (host_wake) {
    ++s_host_wake_services;
    status = jh_cyw43_gspi_host_wake_clear(transport);
  }
  ++s_service_calls;
  s_in_service = false;
  return status;
}

extern "C" hal_status_t jh_cyw43_lwip_join_start(const char *ssid,
                                                 const char *password,
                                                 uint32_t auth_type) {
  if (ssid == nullptr || password == nullptr) {
    return HAL_EINVAL;
  }
  const size_t ssid_length = strlen(ssid);
  const size_t password_length = strlen(password);
  if (ssid_length == 0u || ssid_length > 32u || password_length > 64u) {
    return HAL_EINVAL;
  }
  if (!jh_cyw43_driver_is_ready() || !netif_is_owned()) {
    return HAL_EUNINIT;
  }

  const int join_status = cyw43_wifi_join(
      &cyw43_state, ssid_length, reinterpret_cast<const uint8_t *>(ssid),
      password_length, reinterpret_cast<const uint8_t *>(password), auth_type,
      nullptr, 0u);
  s_last_cyw43_error = join_status;
  if (join_status != 0) {
    return status_from_cyw43(join_status);
  }
  return HAL_OK;
}

extern "C" hal_status_t jh_cyw43_lwip_join(const char *ssid,
                                           const char *password,
                                           uint32_t auth_type,
                                           uint32_t timeout_ms) {
  if (timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  const hal_status_t start_status =
      jh_cyw43_lwip_join_start(ssid, password, auth_type);
  if (start_status != HAL_OK) {
    return start_status;
  }

  const uint32_t started = hal_millis();
  while (!deadline_expired(started, timeout_ms)) {
    const hal_status_t service_status = jh_cyw43_lwip_service();
    if (service_status != HAL_OK) {
      return service_status;
    }
    const int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (link == CYW43_LINK_UP) {
      return HAL_OK;
    }
    if (link == CYW43_LINK_BADAUTH) {
      return HAL_EAUTH;
    }
    if (link == CYW43_LINK_NONET) {
      return HAL_ENOENT;
    }
    if (link == CYW43_LINK_FAIL) {
      return HAL_EIO;
    }
    hal_delay_ms(1u);
  }
  return HAL_ETIMEOUT;
}

extern "C" hal_status_t jh_cyw43_lwip_resolve_ipv4(const char *hostname,
                                                   uint32_t *out_address,
                                                   uint32_t timeout_ms) {
  if (hostname == nullptr || out_address == nullptr || hostname[0] == '\0' ||
      timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  if (!jh_cyw43_driver_is_ready() ||
      cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
    return HAL_ESTATE;
  }

  dns_slot_t *slot = nullptr;
  for (auto &candidate : s_dns_slots) {
    if (!candidate.occupied) {
      slot = &candidate;
      break;
    }
  }
  if (slot == nullptr) {
    return HAL_EBUSY;
  }
  *slot = {true, false, false, 0u};

  ip_addr_t immediate{};
  const err_t dns_status =
      dns_gethostbyname(hostname, &immediate, dns_found, slot);
  if (dns_status == ERR_OK) {
    *out_address = ip_2_ip4(&immediate)->addr;
    slot->occupied = false;
    return HAL_OK;
  }
  if (dns_status != ERR_INPROGRESS) {
    slot->occupied = false;
    return status_from_lwip(dns_status);
  }

  const uint32_t started = hal_millis();
  while (!deadline_expired(started, timeout_ms)) {
    const hal_status_t service_status = jh_cyw43_lwip_service();
    if (service_status != HAL_OK) {
      return service_status;
    }
    if (slot->completed) {
      const bool found = slot->found;
      *out_address = slot->address;
      slot->occupied = false;
      return found ? HAL_OK : HAL_ENOENT;
    }
    hal_delay_ms(1u);
  }
  /* Keep the static slot reserved until lwIP delivers its late callback. This
   * prevents both use-after-return and stale completion of a newer request. */
  return HAL_ETIMEOUT;
}

extern "C" hal_status_t jh_cyw43_lwip_ping_ipv4(uint32_t address,
                                                uint32_t timeout_ms,
                                                int *out_ttl,
                                                uint32_t *out_rtt_ms) {
  if (address == 0u || timeout_ms == 0u || out_ttl == nullptr ||
      out_rtt_ms == nullptr) {
    return HAL_EINVAL;
  }
  if (!jh_cyw43_driver_is_ready() ||
      cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
    return HAL_ESTATE;
  }

  ping_request_t request{++s_ping_sequence, false, -1};
  struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
  const uint16_t packet_size =
      (uint16_t)(sizeof(struct icmp_echo_hdr) + kPingPayloadSize);
  struct pbuf *packet =
      pcb == nullptr ? nullptr : pbuf_alloc(PBUF_IP, packet_size, PBUF_RAM);
  if (pcb == nullptr || packet == nullptr || packet->next != nullptr ||
      packet->len != packet->tot_len) {
    if (packet != nullptr) {
      pbuf_free(packet);
    }
    if (pcb != nullptr) {
      raw_remove(pcb);
    }
    return HAL_ENOMEM;
  }

  pcb->ttl = 64u;
  raw_recv(pcb, ping_received, &request);
  const err_t bind_status = raw_bind(pcb, IP_ADDR_ANY);
  auto *echo = static_cast<struct icmp_echo_hdr *>(packet->payload);
  ICMPH_TYPE_SET(echo, ICMP_ECHO);
  ICMPH_CODE_SET(echo, 0u);
  echo->chksum = 0u;
  echo->id = lwip_htons(kPingIdentifier);
  echo->seqno = lwip_htons(request.sequence);
  auto *bytes = static_cast<uint8_t *>(packet->payload);
  for (uint16_t index = 0u; index < kPingPayloadSize; ++index) {
    bytes[sizeof(*echo) + index] = (uint8_t)('A' + (index % 26u));
  }
  echo->chksum = inet_chksum(echo, packet_size);

  ip_addr_t destination{};
  ip_2_ip4(&destination)->addr = address;
  IP_SET_TYPE_VAL(destination, IPADDR_TYPE_V4);
  const err_t send_status = bind_status == ERR_OK
                                ? raw_sendto(pcb, packet, &destination)
                                : bind_status;
  hal_status_t status = status_from_lwip(send_status);
  const uint32_t started = hal_millis();
  if (send_status == ERR_OK) {
    while (!deadline_expired(started, timeout_ms)) {
      const hal_status_t service_status = jh_cyw43_lwip_service();
      if (service_status != HAL_OK) {
        status = service_status;
        break;
      }
      if (request.completed) {
        *out_ttl = request.ttl;
        *out_rtt_ms = (uint32_t)(hal_millis() - started);
        status = HAL_OK;
        break;
      }
      hal_delay_ms(1u);
    }
    if (!request.completed && status == HAL_OK) {
      status = HAL_ETIMEOUT;
    }
  }

  pbuf_free(packet);
  raw_remove(pcb);
  return status;
}

extern "C" hal_status_t jh_cyw43_lwip_leave(void) {
  if (!jh_cyw43_driver_is_ready()) {
    return HAL_EUNINIT;
  }
  const int status = cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
  s_last_cyw43_error = status;
  if (status != 0) {
    if (status != -CYW43_ETIMEDOUT) {
      return status_from_cyw43(status);
    }
    /*
     * A busy data path can delay the control response beyond the driver's
     * ioctl timeout even though firmware accepted the disassociation.  Only
     * recover that false timeout after the station demonstrably ceases to
     * have a usable TCP/IP link.
     */
    const uint32_t started = hal_millis();
    while (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) ==
               CYW43_LINK_UP &&
           !deadline_expired(started, 1000u)) {
      (void)jh_cyw43_lwip_service();
      hal_delay_ms(1u);
    }
    if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP) {
      return HAL_ETIMEOUT;
    }
  }
  /*
   * cyw43_wifi_leave() is a synchronous control ioctl.  CYW43439 does not
   * guarantee a later DISASSOC event for a host-requested leave, so waiting
   * for that event can report a false timeout while the station already has
   * no usable link.  Mirror the accepted command in the local driver and
   * lwIP state; a later link-down/DISASSOC event is then idempotent.
   */
  cyw43_state.wifi_join_state = 0u;
  netif_set_link_down(&cyw43_state.netif[CYW43_ITF_STA]);
  return HAL_OK;
}

extern "C" hal_status_t
jh_cyw43_lwip_get_snapshot(jh_cyw43_lwip_snapshot_t *out_snapshot) {
  if (out_snapshot == nullptr) {
    return HAL_EINVAL;
  }
  memset(out_snapshot, 0, sizeof(*out_snapshot));
  out_snapshot->initialized = jh_cyw43_driver_is_ready();
  out_snapshot->netif_present = netif_is_owned();
  if (!out_snapshot->initialized) {
    return HAL_OK;
  }

  struct netif *netif = &cyw43_state.netif[CYW43_ITF_STA];
  out_snapshot->wifi_link_status =
      cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
  out_snapshot->tcpip_link_status =
      cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
  out_snapshot->last_cyw43_error = s_last_cyw43_error;
  out_snapshot->link_up = netif_is_link_up(netif);
  out_snapshot->dhcp_bound = out_snapshot->tcpip_link_status == CYW43_LINK_UP;
  out_snapshot->ipv4 = netif_ip4_addr(netif)->addr;
  out_snapshot->netmask = netif_ip4_netmask(netif)->addr;
  out_snapshot->gateway = netif_ip4_gw(netif)->addr;
  const ip_addr_t *dns = dns_getserver(0u);
  out_snapshot->dns =
      dns != nullptr && IP_IS_V4(dns) ? ip_2_ip4(dns)->addr : 0u;
  out_snapshot->generation = jh_cyw43_driver_generation_internal();
  out_snapshot->service_calls = s_service_calls;
  out_snapshot->host_wake_services = s_host_wake_services;
  out_snapshot->lwip_heap_used = lwip_stats.mem.used;
  out_snapshot->lwip_heap_peak = lwip_stats.mem.max;
  for (int index = 0; index < MEMP_MAX; ++index) {
    const struct stats_mem *pool = lwip_stats.memp[index];
    if (pool == nullptr) {
      continue;
    }
    out_snapshot->lwip_pool_used += pool->used;
    out_snapshot->lwip_pool_peak += pool->max;
    out_snapshot->lwip_allocation_errors += pool->err;
  }
  out_snapshot->lwip_allocation_errors += lwip_stats.mem.err;
  return HAL_OK;
}

#else

extern "C" uint32_t jh_lwip_port_rand(void) { return 1u; }
#if HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_RP
extern "C" uint32_t sys_now(void) { return 0u; }
#endif
extern "C" __attribute__((noreturn)) void
jh_lwip_port_assert(const char *, const char *, int) {
  __builtin_trap();
  for (;;) {
  }
}
extern "C" hal_status_t jh_cyw43_lwip_service(void) { return HAL_EUNSUPPORTED; }
extern "C" hal_status_t jh_cyw43_lwip_join_start(const char *, const char *,
                                                 uint32_t) {
  return HAL_EUNSUPPORTED;
}
extern "C" hal_status_t jh_cyw43_lwip_join(const char *, const char *, uint32_t,
                                           uint32_t) {
  return HAL_EUNSUPPORTED;
}
extern "C" hal_status_t jh_cyw43_lwip_resolve_ipv4(const char *, uint32_t *,
                                                   uint32_t) {
  return HAL_EUNSUPPORTED;
}
extern "C" hal_status_t jh_cyw43_lwip_ping_ipv4(uint32_t, uint32_t, int *,
                                                uint32_t *) {
  return HAL_EUNSUPPORTED;
}
extern "C" hal_status_t jh_cyw43_lwip_leave(void) { return HAL_EUNSUPPORTED; }
extern "C" hal_status_t
jh_cyw43_lwip_get_snapshot(jh_cyw43_lwip_snapshot_t *out_snapshot) {
  if (out_snapshot == nullptr) {
    return HAL_EINVAL;
  }
  memset(out_snapshot, 0, sizeof(*out_snapshot));
  return HAL_EUNSUPPORTED;
}

#endif
