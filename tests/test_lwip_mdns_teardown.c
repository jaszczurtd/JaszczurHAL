#include "utils/unity.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uint8_t u8_t;
typedef int err_t;

#define ERR_OK 0
#define ERR_VAL -6
#define LWIP_IPV4 1
#define LWIP_IPV6 1
#define MDNS_MAX_SERVICES 2

struct pbuf {
  u8_t if_idx;
};

struct mdns_service {
  int active;
};

struct mdns_host {
  struct mdns_service *services[MDNS_MAX_SERVICES];
};

struct netif {
  u8_t index;
  struct mdns_host *mdns;
};

struct mdns_packet {
  struct pbuf *pbuf;
  struct mdns_packet *next_answer;
  struct mdns_packet *next_tc_question;
};

static struct mdns_packet *pending_tc_questions;

enum event_kind {
  EVENT_UNTIMEOUT,
  EVENT_PBUF_FREE,
  EVENT_PACKET_FREE,
  EVENT_SET_CLIENT_DATA,
  EVENT_LEAVE_IPV4,
  EVENT_LEAVE_IPV6,
  EVENT_MEM_FREE,
};

struct teardown_event {
  enum event_kind kind;
  const char *handler;
  const void *argument;
};

static struct teardown_event s_events[64];
static size_t s_event_count;
static size_t s_core_lock_checks;

static void record_event(enum event_kind kind, const char *handler,
                         const void *argument) {
  TEST_ASSERT_LESS_THAN_UINT32(sizeof(s_events) / sizeof(s_events[0]),
                               s_event_count);
  s_events[s_event_count].kind = kind;
  s_events[s_event_count].handler = handler;
  s_events[s_event_count].argument = argument;
  ++s_event_count;
}

static void record_untimeout(const char *handler, const void *argument) {
  record_event(EVENT_UNTIMEOUT, handler, argument);
}

static void record_pbuf_free(const void *buffer) {
  record_event(EVENT_PBUF_FREE, NULL, buffer);
}

static void record_packet_free(const void *packet) {
  record_event(EVENT_PACKET_FREE, NULL, packet);
}

static void record_set_client_data(struct netif *netif,
                                   struct mdns_host *data) {
  record_event(EVENT_SET_CLIENT_DATA, NULL, netif);
  netif->mdns = data;
}

static void record_leave(enum event_kind kind, struct netif *netif) {
  TEST_ASSERT_NULL(netif->mdns);
  record_event(kind, NULL, netif);
}

static void record_mem_free(const void *memory) {
  record_event(EVENT_MEM_FREE, NULL, memory);
}

#define netif_get_index(netif) ((netif)->index)
#define NETIF_TO_HOST(netif) ((netif)->mdns)
#define LWIP_ASSERT_CORE_LOCKED() (++s_core_lock_checks)
#define LWIP_ASSERT(message, expression) TEST_ASSERT_TRUE(expression)
#define LWIP_ERROR(message, expression, handler)                               \
  do {                                                                         \
    if (!(expression)) {                                                       \
      handler;                                                                 \
    }                                                                          \
  } while (0)
#define JH_LWIP_MDNS_UNTIMEOUT(handler, argument)                              \
  record_untimeout(#handler, argument)
#define JH_LWIP_MDNS_PBUF_FREE(buffer) record_pbuf_free(buffer)
#define JH_LWIP_MDNS_PACKET_FREE(packet) record_packet_free(packet)
#define JH_LWIP_MDNS_SET_CLIENT_DATA(netif, data)                              \
  record_set_client_data((netif), (data))
#define JH_LWIP_MDNS_LEAVE_IPV4(netif) record_leave(EVENT_LEAVE_IPV4, (netif))
#define JH_LWIP_MDNS_LEAVE_IPV6(netif) record_leave(EVENT_LEAVE_IPV6, (netif))
#define JH_LWIP_MDNS_MEM_FREE(memory) record_mem_free(memory)

#include "jh_lwip_mdns_teardown.inc"

static size_t count_events(enum event_kind kind, const char *handler,
                           const void *argument) {
  size_t count = 0u;
  size_t i;
  for (i = 0u; i < s_event_count; ++i) {
    if ((s_events[i].kind == kind) &&
        ((handler == NULL) || ((s_events[i].handler != NULL) &&
                               (strcmp(s_events[i].handler, handler) == 0))) &&
        ((argument == NULL) || (s_events[i].argument == argument))) {
      ++count;
    }
  }
  return count;
}

static size_t first_event(enum event_kind kind, const char *handler,
                          const void *argument) {
  size_t i;
  for (i = 0u; i < s_event_count; ++i) {
    if ((s_events[i].kind == kind) &&
        ((handler == NULL) || ((s_events[i].handler != NULL) &&
                               (strcmp(s_events[i].handler, handler) == 0))) &&
        ((argument == NULL) || (s_events[i].argument == argument))) {
      return i;
    }
  }
  return s_event_count;
}

void setUp(void) {
  pending_tc_questions = NULL;
  memset(s_events, 0, sizeof(s_events));
  s_event_count = 0u;
  s_core_lock_checks = 0u;
}

void tearDown(void) {}

static void test_remove_detaches_before_leave_and_releases_state_last(void) {
  static const char *const expected_handlers[] = {
      "mdns_probe_and_announce",
      "mdns_multicast_timeout_reset_ipv4",
      "mdns_multicast_probe_timeout_reset_ipv4",
      "mdns_multicast_timeout_25ttl_reset_ipv4",
      "mdns_send_multicast_msg_delayed_ipv4",
      "mdns_send_unicast_msg_delayed_ipv4",
      "mdns_multicast_timeout_reset_ipv6",
      "mdns_multicast_probe_timeout_reset_ipv6",
      "mdns_multicast_timeout_25ttl_reset_ipv6",
      "mdns_send_multicast_msg_delayed_ipv6",
      "mdns_send_unicast_msg_delayed_ipv6",
  };
  struct mdns_service first_service = {1};
  struct mdns_service second_service = {1};
  struct mdns_host host = {{&first_service, &second_service}};
  struct netif interface = {3u, &host};
  size_t detach_at;
  size_t leave_ipv4_at;
  size_t leave_ipv6_at;
  size_t service_free_at;
  size_t timeout_at;
  size_t i;

  TEST_ASSERT_EQUAL_INT(ERR_OK, mdns_resp_remove_netif(&interface));
  TEST_ASSERT_NULL(interface.mdns);
  TEST_ASSERT_EQUAL_UINT32(1u, s_core_lock_checks);
  TEST_ASSERT_EQUAL_UINT32(
      1u, count_events(EVENT_SET_CLIENT_DATA, NULL, &interface));
  TEST_ASSERT_EQUAL_UINT32(1u,
                           count_events(EVENT_LEAVE_IPV4, NULL, &interface));
  TEST_ASSERT_EQUAL_UINT32(1u,
                           count_events(EVENT_LEAVE_IPV6, NULL, &interface));
  detach_at = first_event(EVENT_SET_CLIENT_DATA, NULL, &interface);
  leave_ipv4_at = first_event(EVENT_LEAVE_IPV4, NULL, &interface);
  leave_ipv6_at = first_event(EVENT_LEAVE_IPV6, NULL, &interface);
  for (i = 0u; i < sizeof(expected_handlers) / sizeof(expected_handlers[0]);
       ++i) {
    TEST_ASSERT_EQUAL_UINT32(
        1u, count_events(EVENT_UNTIMEOUT, expected_handlers[i], &interface));
    timeout_at = first_event(EVENT_UNTIMEOUT, expected_handlers[i], &interface);
    TEST_ASSERT_LESS_THAN_UINT32(leave_ipv4_at, timeout_at);
    TEST_ASSERT_LESS_THAN_UINT32(leave_ipv6_at, timeout_at);
  }
  TEST_ASSERT_EQUAL_UINT32(1u,
                           count_events(EVENT_MEM_FREE, NULL, &first_service));
  TEST_ASSERT_EQUAL_UINT32(1u,
                           count_events(EVENT_MEM_FREE, NULL, &second_service));
  TEST_ASSERT_EQUAL_UINT32(1u, count_events(EVENT_MEM_FREE, NULL, &host));

  service_free_at = first_event(EVENT_MEM_FREE, NULL, &first_service);
  TEST_ASSERT_EQUAL_UINT32(0u, detach_at);
  TEST_ASSERT_LESS_THAN_UINT32(leave_ipv4_at, detach_at);
  TEST_ASSERT_LESS_THAN_UINT32(leave_ipv6_at, detach_at);
  TEST_ASSERT_LESS_THAN_UINT32(service_free_at, leave_ipv4_at);
  TEST_ASSERT_LESS_THAN_UINT32(service_free_at, leave_ipv6_at);
  TEST_ASSERT_EQUAL_INT(EVENT_MEM_FREE, s_events[s_event_count - 1u].kind);
  TEST_ASSERT_EQUAL_PTR(&host, s_events[s_event_count - 1u].argument);
}

static void test_remove_drains_only_matching_truncated_questions(void) {
  struct mdns_host first_host = {{NULL, NULL}};
  struct mdns_host second_host = {{NULL, NULL}};
  struct netif first = {4u, &first_host};
  struct netif second = {7u, &second_host};
  struct pbuf first_question_buffer = {4u};
  struct pbuf first_answer_a_buffer = {4u};
  struct pbuf first_answer_b_buffer = {4u};
  struct pbuf first_question_b_buffer = {4u};
  struct pbuf second_question_buffer = {7u};
  struct pbuf second_answer_buffer = {7u};
  struct mdns_packet first_answer_b = {&first_answer_b_buffer, NULL, NULL};
  struct mdns_packet first_answer_a = {&first_answer_a_buffer, &first_answer_b,
                                       NULL};
  struct mdns_packet first_question_b = {&first_question_b_buffer, NULL, NULL};
  struct mdns_packet second_answer = {&second_answer_buffer, NULL, NULL};
  struct mdns_packet second_question = {&second_question_buffer, &second_answer,
                                        &first_question_b};
  struct mdns_packet first_question = {&first_question_buffer, &first_answer_a,
                                       &second_question};
  size_t tc_cancel_at;
  size_t first_free_at;
  size_t leave_ipv4_at;

  pending_tc_questions = &first_question;

  TEST_ASSERT_EQUAL_INT(ERR_OK, mdns_resp_remove_netif(&first));
  TEST_ASSERT_EQUAL_PTR(&second_question, pending_tc_questions);
  TEST_ASSERT_NULL(second_question.next_tc_question);
  TEST_ASSERT_NULL(first.mdns);
  TEST_ASSERT_NOT_NULL(second.mdns);
  TEST_ASSERT_EQUAL_UINT32(
      2u, count_events(EVENT_UNTIMEOUT, "mdns_handle_tc_question", NULL));
  TEST_ASSERT_EQUAL_UINT32(1u, count_events(EVENT_UNTIMEOUT,
                                            "mdns_handle_tc_question",
                                            &first_question));
  TEST_ASSERT_EQUAL_UINT32(1u, count_events(EVENT_UNTIMEOUT,
                                            "mdns_handle_tc_question",
                                            &first_question_b));
  TEST_ASSERT_EQUAL_UINT32(4u, count_events(EVENT_PBUF_FREE, NULL, NULL));
  TEST_ASSERT_EQUAL_UINT32(4u, count_events(EVENT_PACKET_FREE, NULL, NULL));
  TEST_ASSERT_EQUAL_UINT32(
      0u, count_events(EVENT_PBUF_FREE, NULL, &second_question_buffer));
  TEST_ASSERT_EQUAL_UINT32(
      0u, count_events(EVENT_PACKET_FREE, NULL, &second_question));
  tc_cancel_at =
      first_event(EVENT_UNTIMEOUT, "mdns_handle_tc_question", &first_question);
  first_free_at = first_event(EVENT_PBUF_FREE, NULL, &first_question_buffer);
  leave_ipv4_at = first_event(EVENT_LEAVE_IPV4, NULL, &first);
  TEST_ASSERT_LESS_THAN_UINT32(first_free_at, tc_cancel_at);
  TEST_ASSERT_LESS_THAN_UINT32(leave_ipv4_at, tc_cancel_at);
  TEST_ASSERT_LESS_THAN_UINT32(leave_ipv4_at, first_free_at);
  TEST_ASSERT_EQUAL_INT(EVENT_MEM_FREE, s_events[s_event_count - 1u].kind);
  TEST_ASSERT_EQUAL_PTR(&first_host, s_events[s_event_count - 1u].argument);

  memset(s_events, 0, sizeof(s_events));
  s_event_count = 0u;

  TEST_ASSERT_EQUAL_INT(ERR_OK, mdns_resp_remove_netif(&second));
  TEST_ASSERT_NULL(pending_tc_questions);
  TEST_ASSERT_NULL(second.mdns);
  TEST_ASSERT_EQUAL_UINT32(1u, count_events(EVENT_UNTIMEOUT,
                                            "mdns_handle_tc_question",
                                            &second_question));
  TEST_ASSERT_EQUAL_UINT32(2u, count_events(EVENT_PBUF_FREE, NULL, NULL));
  TEST_ASSERT_EQUAL_UINT32(2u, count_events(EVENT_PACKET_FREE, NULL, NULL));
  TEST_ASSERT_EQUAL_INT(EVENT_MEM_FREE, s_events[s_event_count - 1u].kind);
  TEST_ASSERT_EQUAL_PTR(&second_host, s_events[s_event_count - 1u].argument);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_remove_detaches_before_leave_and_releases_state_last);
  RUN_TEST(test_remove_drains_only_matching_truncated_questions);
  return UNITY_END();
}
