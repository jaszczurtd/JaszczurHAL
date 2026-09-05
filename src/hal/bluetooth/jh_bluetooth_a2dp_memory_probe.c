#include "jh_bluetooth_a2dp_memory_probe.h"

#include "btstack_config.h"
#include "btstack_memory.h"

#include <stddef.h>
#include <string.h>

static jh_bluetooth_a2dp_memory_snapshot_t s_snapshot;

static void allocation_result(jh_bluetooth_a2dp_pool_snapshot_t *pool,
                              const void *allocation) {
  if (allocation == NULL) {
    if (pool->allocation_failures != UINT8_MAX) {
      ++pool->allocation_failures;
    }
    return;
  }
  if (pool->current != UINT8_MAX) {
    ++pool->current;
  }
  if (pool->current > pool->high_water) {
    pool->high_water = pool->current;
  }
}

static void allocation_freed(jh_bluetooth_a2dp_pool_snapshot_t *pool,
                             const void *allocation) {
  if (allocation != NULL && pool->current > 0u) {
    --pool->current;
  }
}

void jh_bluetooth_a2dp_memory_probe_reset(void) {
  memset(&s_snapshot, 0, sizeof(s_snapshot));
  s_snapshot.hci_connections.capacity = MAX_NR_HCI_CONNECTIONS;
  s_snapshot.l2cap_services.capacity = MAX_NR_L2CAP_SERVICES;
  s_snapshot.l2cap_channels.capacity = MAX_NR_L2CAP_CHANNELS;
  s_snapshot.link_keys.capacity = MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES;
  s_snapshot.service_records.capacity = MAX_NR_SERVICE_RECORD_ITEMS;
  s_snapshot.avdtp_endpoints.capacity = MAX_NR_AVDTP_STREAM_ENDPOINTS;
  s_snapshot.avdtp_connections.capacity = MAX_NR_AVDTP_CONNECTIONS;
#ifdef MAX_NR_AVRCP_CONNECTIONS
  s_snapshot.avrcp_connections.capacity = MAX_NR_AVRCP_CONNECTIONS;
#endif
}

void jh_bluetooth_a2dp_memory_probe_snapshot(
    jh_bluetooth_a2dp_memory_snapshot_t *out_snapshot) {
  if (out_snapshot != NULL) {
    *out_snapshot = s_snapshot;
  }
}

#define JH_DECLARE_WRAPPER(name, type, field)                                  \
  type *__real_btstack_memory_##name##_get(void);                              \
  void __real_btstack_memory_##name##_free(type *allocation);                  \
  type *__wrap_btstack_memory_##name##_get(void) {                             \
    type *allocation = __real_btstack_memory_##name##_get();                   \
    allocation_result(&s_snapshot.field, allocation);                          \
    return allocation;                                                         \
  }                                                                            \
  void __wrap_btstack_memory_##name##_free(type *allocation) {                 \
    allocation_freed(&s_snapshot.field, allocation);                           \
    __real_btstack_memory_##name##_free(allocation);                           \
  }

JH_DECLARE_WRAPPER(hci_connection, hci_connection_t, hci_connections)
JH_DECLARE_WRAPPER(l2cap_service, l2cap_service_t, l2cap_services)
JH_DECLARE_WRAPPER(l2cap_channel, l2cap_channel_t, l2cap_channels)
JH_DECLARE_WRAPPER(btstack_link_key_db_memory_entry,
                   btstack_link_key_db_memory_entry_t, link_keys)
JH_DECLARE_WRAPPER(service_record_item, service_record_item_t, service_records)
JH_DECLARE_WRAPPER(avdtp_stream_endpoint, avdtp_stream_endpoint_t,
                   avdtp_endpoints)
JH_DECLARE_WRAPPER(avdtp_connection, avdtp_connection_t, avdtp_connections)
JH_DECLARE_WRAPPER(avrcp_connection, avrcp_connection_t, avrcp_connections)

#undef JH_DECLARE_WRAPPER
