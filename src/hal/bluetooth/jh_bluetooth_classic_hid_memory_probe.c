#include "jh_bluetooth_classic_hid_memory_probe.h"

#include "btstack_config.h"
#include "btstack_memory.h"

#include <stddef.h>
#include <string.h>

static jh_bluetooth_classic_hid_memory_snapshot_t s_snapshot;

static void allocation_result(jh_bluetooth_classic_hid_pool_snapshot_t *pool,
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

static void allocation_freed(jh_bluetooth_classic_hid_pool_snapshot_t *pool) {
  if (pool->current > 0u) {
    --pool->current;
  }
}

void jh_bluetooth_classic_hid_memory_probe_reset(void) {
  memset(&s_snapshot, 0, sizeof(s_snapshot));
  s_snapshot.l2cap_services.capacity = MAX_NR_L2CAP_SERVICES;
  s_snapshot.l2cap_channels.capacity = MAX_NR_L2CAP_CHANNELS;
  s_snapshot.link_keys.capacity = MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES;
  s_snapshot.hid_connections.capacity = MAX_NR_HID_HOST_CONNECTIONS;
}

void jh_bluetooth_classic_hid_memory_probe_snapshot(
    jh_bluetooth_classic_hid_memory_snapshot_t *out_snapshot) {
  if (out_snapshot != NULL) {
    *out_snapshot = s_snapshot;
  }
}

l2cap_service_t *__real_btstack_memory_l2cap_service_get(void);
void __real_btstack_memory_l2cap_service_free(l2cap_service_t *service);
l2cap_channel_t *__real_btstack_memory_l2cap_channel_get(void);
void __real_btstack_memory_l2cap_channel_free(l2cap_channel_t *channel);
btstack_link_key_db_memory_entry_t *
__real_btstack_memory_btstack_link_key_db_memory_entry_get(void);
void __real_btstack_memory_btstack_link_key_db_memory_entry_free(
    btstack_link_key_db_memory_entry_t *entry);
hid_host_connection_t *__real_btstack_memory_hid_host_connection_get(void);
void __real_btstack_memory_hid_host_connection_free(
    hid_host_connection_t *connection);

l2cap_service_t *__wrap_btstack_memory_l2cap_service_get(void) {
  l2cap_service_t *service = __real_btstack_memory_l2cap_service_get();
  allocation_result(&s_snapshot.l2cap_services, service);
  return service;
}

void __wrap_btstack_memory_l2cap_service_free(l2cap_service_t *service) {
  allocation_freed(&s_snapshot.l2cap_services);
  __real_btstack_memory_l2cap_service_free(service);
}

l2cap_channel_t *__wrap_btstack_memory_l2cap_channel_get(void) {
  l2cap_channel_t *channel = __real_btstack_memory_l2cap_channel_get();
  allocation_result(&s_snapshot.l2cap_channels, channel);
  return channel;
}

void __wrap_btstack_memory_l2cap_channel_free(l2cap_channel_t *channel) {
  allocation_freed(&s_snapshot.l2cap_channels);
  __real_btstack_memory_l2cap_channel_free(channel);
}

btstack_link_key_db_memory_entry_t *
__wrap_btstack_memory_btstack_link_key_db_memory_entry_get(void) {
  btstack_link_key_db_memory_entry_t *entry =
      __real_btstack_memory_btstack_link_key_db_memory_entry_get();
  allocation_result(&s_snapshot.link_keys, entry);
  return entry;
}

void __wrap_btstack_memory_btstack_link_key_db_memory_entry_free(
    btstack_link_key_db_memory_entry_t *entry) {
  allocation_freed(&s_snapshot.link_keys);
  __real_btstack_memory_btstack_link_key_db_memory_entry_free(entry);
}

hid_host_connection_t *__wrap_btstack_memory_hid_host_connection_get(void) {
  hid_host_connection_t *connection =
      __real_btstack_memory_hid_host_connection_get();
  allocation_result(&s_snapshot.hid_connections, connection);
  return connection;
}

void __wrap_btstack_memory_hid_host_connection_free(
    hid_host_connection_t *connection) {
  allocation_freed(&s_snapshot.hid_connections);
  __real_btstack_memory_hid_host_connection_free(connection);
}
