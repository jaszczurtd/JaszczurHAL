#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_RP
#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_OTA

#include "hal/core/hal_mutex_once.h"
#include "hal/network/cyw43/jh_cyw43_mdns.h"
#include "hal/network/hal_net.h"
#include "hal/network/hal_tcp.h"
#include "hal/network/hal_udp.h"
#include "hal/network/jh_cyw43_provider.h"
#include "hal/network/ota/hal_ota.h"
#include "hal/network/ota/jh_ota_protocol.h"
#include "hal/security/hal_crypto.h"
#include "hal/security/jh_secure_random.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_sync.h"
#include "hal/system/hal_system.h"

#include "drivers/flash/rp_ota_storage.h"
#include <hardware/watchdog.h>
#include <lwip/netif.h>
#include <stdio.h>
#include <string.h>

#define HAL_OTA_TEXT_BUF_SIZE 96u
#define HAL_OTA_EVENT_QUEUE_SIZE 12u
#define HAL_OTA_UDP_BUFFER_SIZE 192u
#define HAL_OTA_TCP_BUFFER_SIZE 1024u
#define HAL_OTA_DEFAULT_PORT 8266u
#define HAL_OTA_CONNECT_TIMEOUT_MS 5000u
#define HAL_OTA_RECEIVE_TIMEOUT_MS 5000u
#define HAL_OTA_HOSTNAME_MAX_LENGTH 63u

typedef enum {
  HAL_OTA_EVENT_NONE = 0,
  HAL_OTA_EVENT_START,
  HAL_OTA_EVENT_END,
  HAL_OTA_EVENT_PROGRESS,
  HAL_OTA_EVENT_ERROR
} hal_ota_event_type_t;

typedef enum {
  HAL_OTA_STATE_IDLE = 0,
  HAL_OTA_STATE_WAIT_AUTH,
  HAL_OTA_STATE_BEGIN_TRANSFER,
  HAL_OTA_STATE_RECEIVE
} hal_ota_state_t;

typedef struct {
  hal_ota_event_type_t type;
  hal_ota_command_t command;
  hal_ota_error_t error;
  uint32_t progress;
  uint32_t total;
} hal_ota_event_t;

static struct {
  hal_mutex_t mutex;
  bool started;
  bool password_set;
  bool reboot_pending;
  hal_ota_state_t state;

  uint16_t port;
  char hostname[HAL_OTA_TEXT_BUF_SIZE];
  char password_md5[JH_OTA_MD5_HEX_BUFFER_SIZE];
  char nonce[JH_OTA_MD5_HEX_BUFFER_SIZE];

  hal_udp_socket_t udp;
  hal_tcp_socket_t tcp;
  hal_net_endpoint_t remote_udp;
  jh_ota_invitation_t invitation;
  uint32_t received;
  uint32_t last_activity_ms;

  hal_ota_on_start_callback_t on_start;
  void *on_start_user;
  hal_ota_on_end_callback_t on_end;
  void *on_end_user;
  hal_ota_on_progress_callback_t on_progress;
  void *on_progress_user;
  hal_ota_on_error_callback_t on_error;
  void *on_error_user;

  hal_ota_event_t queue[HAL_OTA_EVENT_QUEUE_SIZE];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
} s_ota;

static bool update_begin_no_lock(void) {
  if (s_ota.invitation.command != 0u) {
    return false;
  }
  const uint8_t *key =
      s_ota.password_set ? reinterpret_cast<const uint8_t *>(s_ota.password_md5)
                         : nullptr;
  const size_t key_size = s_ota.password_set ? strlen(s_ota.password_md5) : 0u;
  return jh_rp_ota_storage_begin(s_ota.invitation.image_size, key, key_size) ==
         HAL_OK;
}

static size_t update_write_no_lock(const uint8_t *data, size_t size) {
  size_t written = 0u;
  return jh_rp_ota_storage_write(data, size, &written) == HAL_OK ? written : 0u;
}

static bool update_end_no_lock(void) {
  return jh_rp_ota_storage_finish() == HAL_OK;
}

static void update_abort_no_lock(void) { jh_rp_ota_storage_abort(); }

static bool ota_ensure_mutex(void) {
  return jh_hal_mutex_create_once(&s_ota.mutex) != nullptr;
}

static hal_status_t publish_mdns_no_lock(const char *hostname) {
  hal_status_t status = jh_cyw43_provider_stack_enter(false);
  if (status != HAL_OK) {
    return status;
  }
  struct netif *netif = jh_cyw43_provider_netif();
  status =
      netif == nullptr ? HAL_EUNINIT : jh_cyw43_mdns_publish(netif, hostname);
  jh_cyw43_provider_stack_leave();
  return status;
}

static bool validate_non_empty(const char *value, const char *fn,
                               const char *name) {
  if (!value || value[0] == '\0') {
    hal_derr("%s: %s is NULL/empty", fn, name);
    return false;
  }
  return true;
}

static void queue_clear_no_lock(void) {
  s_ota.head = 0u;
  s_ota.tail = 0u;
  s_ota.count = 0u;
}

static void queue_push_no_lock(const hal_ota_event_t *event_in) {
  if (!event_in) {
    return;
  }

  if (s_ota.count >= HAL_OTA_EVENT_QUEUE_SIZE) {
    s_ota.head = (uint8_t)((s_ota.head + 1u) % HAL_OTA_EVENT_QUEUE_SIZE);
    s_ota.count--;
  }

  s_ota.queue[s_ota.tail] = *event_in;
  s_ota.tail = (uint8_t)((s_ota.tail + 1u) % HAL_OTA_EVENT_QUEUE_SIZE);
  s_ota.count++;
}

static bool queue_pop_no_lock(hal_ota_event_t *event_out) {
  if (!event_out || s_ota.count == 0u) {
    return false;
  }

  *event_out = s_ota.queue[s_ota.head];
  s_ota.head = (uint8_t)((s_ota.head + 1u) % HAL_OTA_EVENT_QUEUE_SIZE);
  s_ota.count--;
  return true;
}

static void queue_error_no_lock(hal_ota_error_t error) {
  hal_ota_event_t event{};
  event.type = HAL_OTA_EVENT_ERROR;
  event.error = error;
  queue_push_no_lock(&event);
}

static bool udp_send_no_lock(const char *text) {
  if (s_ota.udp == nullptr || text == nullptr) {
    return false;
  }
  const size_t length = strlen(text);
  size_t sent = 0u;
  return hal_udp_socket_sendto_ex(s_ota.udp, text, length, &s_ota.remote_udp,
                                  &sent) == HAL_OK &&
         sent == length;
}

static bool tcp_send_no_lock(const char *text) {
  if (s_ota.tcp == nullptr || text == nullptr) {
    return false;
  }
  const size_t length = strlen(text);
  size_t offset = 0u;
  while (offset < length) {
    size_t sent = 0u;
    if (hal_tcp_socket_send_ex(s_ota.tcp, text + offset, length - offset,
                               &sent) != HAL_OK ||
        sent == 0u) {
      return false;
    }
    offset += sent;
  }
  return true;
}

static void transfer_reset_no_lock(void) {
  if (s_ota.tcp != nullptr) {
    hal_tcp_socket_close(s_ota.tcp);
    s_ota.tcp = nullptr;
  }
  s_ota.received = 0u;
  memset(&s_ota.invitation, 0, sizeof(s_ota.invitation));
  memset(&s_ota.remote_udp, 0, sizeof(s_ota.remote_udp));
  s_ota.state = HAL_OTA_STATE_IDLE;
}

static void transfer_fail_no_lock(hal_ota_error_t error,
                                  const char *udp_message) {
  update_abort_no_lock();
  if (udp_message != nullptr) {
    (void)udp_send_no_lock(udp_message);
  }
  queue_error_no_lock(error);
  transfer_reset_no_lock();
}

static bool make_nonce_no_lock(void) {
  uint8_t random[16]{};
  const bool generated =
      jh_secure_random_bytes(random, sizeof(random)) == HAL_OK &&
      hal_md5_hex(random, sizeof(random), s_ota.nonce, sizeof(s_ota.nonce));
  jh_secure_zeroize(random, sizeof(random));
  return generated;
}

static bool authenticate_no_lock(const jh_ota_auth_response_t *response) {
  if (response == nullptr) {
    return false;
  }
  char transcript[JH_OTA_AUTH_TRANSCRIPT_BUFFER_SIZE]{};
  size_t transcript_length = 0u;
  if (jh_ota_format_auth_transcript(
          &s_ota.invitation, s_ota.nonce, response->client_nonce, transcript,
          sizeof(transcript), &transcript_length) != HAL_OK) {
    return false;
  }

  char expected[JH_OTA_AUTH_TAG_HEX_BUFFER_SIZE]{};
  const bool hashed = hal_hmac_sha256_hex(
      reinterpret_cast<const uint8_t *>(s_ota.password_md5),
      JH_OTA_MD5_HEX_CHARS, reinterpret_cast<const uint8_t *>(transcript),
      transcript_length, expected, sizeof(expected));
  jh_secure_zeroize(transcript, sizeof(transcript));
  const bool authenticated =
      hashed && jh_ota_auth_tag_equal(expected, response->response);
  jh_secure_zeroize(expected, sizeof(expected));
  return authenticated;
}

static void invitation_received_no_lock(const jh_ota_invitation_t *invitation,
                                        const hal_net_endpoint_t *remote) {
  if (invitation == nullptr || remote == nullptr ||
      remote->family != HAL_NET_AF_INET ||
      remote->addr_len != HAL_NET_IPV4_ADDR_LEN) {
    return;
  }
  s_ota.invitation = *invitation;
  s_ota.remote_udp = *remote;
  s_ota.last_activity_ms = hal_millis();

  if (!s_ota.password_set) {
    s_ota.state = HAL_OTA_STATE_BEGIN_TRANSFER;
    return;
  }
  if (!make_nonce_no_lock()) {
    transfer_fail_no_lock(HAL_OTA_ERROR_AUTH, "Authentication Failed");
    return;
  }

  char response[6u + JH_OTA_MD5_HEX_BUFFER_SIZE]{};
  const int length =
      snprintf(response, sizeof(response), "AUTH2 %s", s_ota.nonce);
  if (length <= 0 || (size_t)length >= sizeof(response) ||
      !udp_send_no_lock(response)) {
    transfer_fail_no_lock(HAL_OTA_ERROR_AUTH, nullptr);
    return;
  }
  s_ota.state = HAL_OTA_STATE_WAIT_AUTH;
}

static void udp_service_no_lock(void) {
  uint8_t packet[HAL_OTA_UDP_BUFFER_SIZE]{};
  hal_net_endpoint_t remote{};
  size_t received = 0u;
  const hal_status_t status = hal_udp_socket_recvfrom_ex(
      s_ota.udp, packet, sizeof(packet), &remote, 0u, &received);
  if (status != HAL_OK || received == 0u) {
    return;
  }

  if (s_ota.state == HAL_OTA_STATE_IDLE) {
    static const char discovery[] = "JHOTA DISCOVER 1";
    if (received == sizeof(discovery) - 1u &&
        memcmp(packet, discovery, sizeof(discovery) - 1u) == 0) {
      s_ota.remote_udp = remote;
      jh_ota_boot_state_t boot_state{};
      const hal_status_t state_status =
          jh_rp_ota_storage_get_state(&boot_state);
      char response[HAL_OTA_UDP_BUFFER_SIZE]{};
      const int length = snprintf(
          response, sizeof(response), "JHOTA 1 %s %s %u %lu %lu %u",
          s_ota.hostname, HAL_TARGET_NAME, (unsigned)s_ota.port,
          (unsigned long)HAL_RP_OTA_SLOT_SIZE,
          (unsigned long)(state_status == HAL_OK ? boot_state.program_generation
                                                 : 0u),
          (unsigned)(state_status == HAL_OK ? boot_state.mode
                                            : JH_OTA_BOOT_RECOVERY));
      if (length > 0 && (size_t)length < sizeof(response)) {
        (void)udp_send_no_lock(response);
      }
      return;
    }
    jh_ota_invitation_t invitation{};
    if (jh_ota_parse_invitation(packet, received, &invitation) == HAL_OK) {
      invitation_received_no_lock(&invitation, &remote);
    }
    return;
  }

  if (s_ota.state == HAL_OTA_STATE_WAIT_AUTH) {
    if (!jh_ota_endpoint_equal(&remote, &s_ota.remote_udp)) {
      return;
    }
    jh_ota_auth_response_t response{};
    if (jh_ota_parse_auth_response(packet, received, &response) != HAL_OK ||
        !authenticate_no_lock(&response)) {
      transfer_fail_no_lock(HAL_OTA_ERROR_AUTH, "Authentication Failed");
      return;
    }
    s_ota.state = HAL_OTA_STATE_BEGIN_TRANSFER;
  }
}

static hal_ota_command_t current_command_no_lock(void) {
  return s_ota.invitation.command == 0u ? HAL_OTA_COMMAND_SKETCH
                                        : HAL_OTA_COMMAND_FILESYSTEM;
}

static void begin_transfer_no_lock(void) {
  if (!update_begin_no_lock()) {
    transfer_fail_no_lock(HAL_OTA_ERROR_BEGIN, "ERR: Update Begin");
    return;
  }
  if (!udp_send_no_lock("OK")) {
    transfer_fail_no_lock(HAL_OTA_ERROR_CONNECT, nullptr);
    return;
  }

  hal_tcp_socket_t socket = nullptr;
  if (hal_tcp_socket_open_ex(&socket) != HAL_OK) {
    transfer_fail_no_lock(HAL_OTA_ERROR_CONNECT, nullptr);
    return;
  }
  hal_net_endpoint_t remote = s_ota.remote_udp;
  remote.port = s_ota.invitation.tcp_port;
  if (hal_tcp_socket_connect_ex(socket, &remote, HAL_OTA_CONNECT_TIMEOUT_MS) !=
      HAL_OK) {
    hal_tcp_socket_close(socket);
    transfer_fail_no_lock(HAL_OTA_ERROR_CONNECT, nullptr);
    return;
  }
  s_ota.tcp = socket;
  s_ota.received = 0u;
  s_ota.last_activity_ms = hal_millis();

  hal_ota_event_t start{};
  start.type = HAL_OTA_EVENT_START;
  start.command = current_command_no_lock();
  queue_push_no_lock(&start);

  hal_ota_event_t progress{};
  progress.type = HAL_OTA_EVENT_PROGRESS;
  progress.total = s_ota.invitation.image_size;
  queue_push_no_lock(&progress);
  s_ota.state = HAL_OTA_STATE_RECEIVE;
}

static void receive_transfer_no_lock(void) {
  const size_t remaining =
      (size_t)(s_ota.invitation.image_size - s_ota.received);
  uint8_t buffer[HAL_OTA_TCP_BUFFER_SIZE]{};
  const size_t capacity =
      remaining < sizeof(buffer) ? remaining : sizeof(buffer);
  size_t received = 0u;
  const hal_status_t status =
      hal_tcp_socket_recv_ex(s_ota.tcp, buffer, capacity, 0u, &received);
  if (status != HAL_OK && status != HAL_EAGAIN && status != HAL_ETIMEOUT) {
    transfer_fail_no_lock(HAL_OTA_ERROR_RECEIVE, nullptr);
    return;
  }

  if (received == 0u) {
    if (!hal_tcp_socket_is_connected(s_ota.tcp) ||
        hal_millis_deadline_expired(s_ota.last_activity_ms,
                                    HAL_OTA_RECEIVE_TIMEOUT_MS)) {
      transfer_fail_no_lock(HAL_OTA_ERROR_RECEIVE, nullptr);
    }
    return;
  }

  s_ota.last_activity_ms = hal_millis();
  const size_t written = update_write_no_lock(buffer, received);
  if (written != received) {
    transfer_fail_no_lock(HAL_OTA_ERROR_RECEIVE, nullptr);
    return;
  }
  s_ota.received += (uint32_t)written;

  char acknowledgement[16]{};
  const int acknowledgement_length =
      snprintf(acknowledgement, sizeof(acknowledgement), "%lu\n",
               (unsigned long)written);
  if (acknowledgement_length <= 0 ||
      (size_t)acknowledgement_length >= sizeof(acknowledgement) ||
      !tcp_send_no_lock(acknowledgement)) {
    transfer_fail_no_lock(HAL_OTA_ERROR_RECEIVE, nullptr);
    return;
  }

  hal_ota_event_t progress{};
  progress.type = HAL_OTA_EVENT_PROGRESS;
  progress.progress = s_ota.received;
  progress.total = s_ota.invitation.image_size;
  queue_push_no_lock(&progress);

  if (s_ota.received < s_ota.invitation.image_size) {
    return;
  }
  if (!update_end_no_lock()) {
    transfer_fail_no_lock(HAL_OTA_ERROR_END, nullptr);
    return;
  }
  (void)tcp_send_no_lock("OK");

  hal_ota_event_t end{};
  end.type = HAL_OTA_EVENT_END;
  queue_push_no_lock(&end);
  s_ota.reboot_pending = true;
  transfer_reset_no_lock();
}

static void dispatch_events(void) {
  for (;;) {
    hal_ota_event_t event{};

    hal_ota_on_start_callback_t on_start = nullptr;
    void *on_start_user = nullptr;
    hal_ota_on_end_callback_t on_end = nullptr;
    void *on_end_user = nullptr;
    hal_ota_on_progress_callback_t on_progress = nullptr;
    void *on_progress_user = nullptr;
    hal_ota_on_error_callback_t on_error = nullptr;
    void *on_error_user = nullptr;

    hal_mutex_lock(s_ota.mutex);
    const bool has_event = queue_pop_no_lock(&event);
    if (has_event) {
      on_start = s_ota.on_start;
      on_start_user = s_ota.on_start_user;
      on_end = s_ota.on_end;
      on_end_user = s_ota.on_end_user;
      on_progress = s_ota.on_progress;
      on_progress_user = s_ota.on_progress_user;
      on_error = s_ota.on_error;
      on_error_user = s_ota.on_error_user;
    }
    hal_mutex_unlock(s_ota.mutex);

    if (!has_event) {
      return;
    }
    switch (event.type) {
    case HAL_OTA_EVENT_START:
      if (on_start != nullptr) {
        on_start(event.command, on_start_user);
      }
      break;
    case HAL_OTA_EVENT_END:
      if (on_end != nullptr) {
        on_end(on_end_user);
      }
      break;
    case HAL_OTA_EVENT_PROGRESS:
      if (on_progress != nullptr) {
        on_progress(event.progress, event.total, on_progress_user);
      }
      break;
    case HAL_OTA_EVENT_ERROR:
      if (on_error != nullptr) {
        on_error(event.error, on_error_user);
      }
      break;
    default:
      break;
    }
  }
}

bool hal_ota_set_port(uint16_t port) {
  if (port == 0u) {
    hal_derr("hal_ota_set_port: port must be > 0");
    return false;
  }
  if (!ota_ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  const bool accepted = !s_ota.started;
  if (accepted) {
    s_ota.port = port;
  }
  hal_mutex_unlock(s_ota.mutex);
  return accepted;
}

bool hal_ota_set_hostname(const char *hostname) {
  if (!validate_non_empty(hostname, "hal_ota_set_hostname", "hostname")) {
    return false;
  }
  const size_t length = strlen(hostname);
  if (length > HAL_OTA_HOSTNAME_MAX_LENGTH) {
    hal_derr("hal_ota_set_hostname: hostname exceeds 63 bytes");
    return false;
  }
  if (!ota_ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  bool accepted = true;
  if (s_ota.started) {
    accepted = publish_mdns_no_lock(hostname) == HAL_OK;
  }
  if (accepted) {
    memcpy(s_ota.hostname, hostname, length + 1u);
  }
  hal_mutex_unlock(s_ota.mutex);
  return accepted;
}

bool hal_ota_set_password(const char *password) {
  if (!password) {
    hal_derr("hal_ota_set_password: password pointer is NULL");
    return false;
  }
  if (!ota_ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  const hal_status_t status =
      jh_ota_derive_password_key(password, s_ota.password_md5);
  if (status == HAL_OK) {
    s_ota.password_set = password[0] != '\0';
  }
  hal_mutex_unlock(s_ota.mutex);
  return status == HAL_OK;
}

bool hal_ota_on_start(hal_ota_on_start_callback_t callback, void *user) {
  if (!ota_ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  s_ota.on_start = callback;
  s_ota.on_start_user = user;
  hal_mutex_unlock(s_ota.mutex);
  return true;
}

bool hal_ota_on_end(hal_ota_on_end_callback_t callback, void *user) {
  if (!ota_ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  s_ota.on_end = callback;
  s_ota.on_end_user = user;
  hal_mutex_unlock(s_ota.mutex);
  return true;
}

bool hal_ota_on_progress(hal_ota_on_progress_callback_t callback, void *user) {
  if (!ota_ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  s_ota.on_progress = callback;
  s_ota.on_progress_user = user;
  hal_mutex_unlock(s_ota.mutex);
  return true;
}

bool hal_ota_on_error(hal_ota_on_error_callback_t callback, void *user) {
  if (!ota_ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  s_ota.on_error = callback;
  s_ota.on_error_user = user;
  hal_mutex_unlock(s_ota.mutex);
  return true;
}

bool hal_ota_begin(void) {
  if (!ota_ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);

  if (s_ota.udp != nullptr) {
    hal_udp_socket_close(s_ota.udp);
    s_ota.udp = nullptr;
  }
  transfer_reset_no_lock();
  queue_clear_no_lock();
  s_ota.reboot_pending = false;
  if (s_ota.port == 0u) {
    s_ota.port = HAL_OTA_DEFAULT_PORT;
  }
  if (s_ota.hostname[0] == '\0') {
    (void)snprintf(s_ota.hostname, sizeof(s_ota.hostname), "%s",
                   HAL_TARGET_NAME);
  }

  hal_udp_socket_t socket = nullptr;
  hal_status_t status = hal_udp_socket_open_ex(&socket);
  if (status == HAL_OK) {
    hal_net_endpoint_t local{};
    local.family = HAL_NET_AF_INET;
    local.addr_len = HAL_NET_IPV4_ADDR_LEN;
    local.port = s_ota.port;
    status = hal_udp_socket_bind_ex(socket, &local);
  }
  if (status == HAL_OK) {
    status = publish_mdns_no_lock(s_ota.hostname);
  }
  if (status != HAL_OK) {
    if (socket != nullptr) {
      hal_udp_socket_close(socket);
    }
    s_ota.started = false;
    hal_mutex_unlock(s_ota.mutex);
    return false;
  }

  s_ota.udp = socket;
  s_ota.started = true;
  hal_mutex_unlock(s_ota.mutex);
  return true;
}

void hal_ota_handle(void) {
  if (!ota_ensure_mutex()) {
    return;
  }
  (void)hal_net_service();

  hal_mutex_lock(s_ota.mutex);
  if (!s_ota.started) {
    hal_mutex_unlock(s_ota.mutex);
    return;
  }
  udp_service_no_lock();
  if (s_ota.state == HAL_OTA_STATE_BEGIN_TRANSFER) {
    begin_transfer_no_lock();
  } else if (s_ota.state == HAL_OTA_STATE_RECEIVE) {
    receive_transfer_no_lock();
  } else if (s_ota.state == HAL_OTA_STATE_WAIT_AUTH &&
             hal_millis_deadline_expired(s_ota.last_activity_ms,
                                         HAL_OTA_CONNECT_TIMEOUT_MS)) {
    transfer_fail_no_lock(HAL_OTA_ERROR_AUTH, "Authentication Timeout");
  }
  hal_mutex_unlock(s_ota.mutex);

  dispatch_events();

  hal_mutex_lock(s_ota.mutex);
  const bool reboot = s_ota.reboot_pending;
  s_ota.reboot_pending = false;
  hal_mutex_unlock(s_ota.mutex);
  if (reboot) {
    hal_delay_ms(100u);
    watchdog_reboot(0u, 0u, 10u);
  }
}

bool hal_ota_is_started(void) {
  if (!ota_ensure_mutex()) {
    return false;
  }
  hal_mutex_lock(s_ota.mutex);
  const bool started = s_ota.started;
  hal_mutex_unlock(s_ota.mutex);
  return started;
}

hal_status_t hal_ota_confirm_boot_ex(void) {
  if (!ota_ensure_mutex()) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_ota.mutex);
  const hal_status_t status = jh_rp_ota_storage_confirm_boot();
  hal_mutex_unlock(s_ota.mutex);
  return status;
}

hal_status_t hal_ota_get_boot_info_ex(hal_ota_boot_info_t *out_info) {
  if (out_info == nullptr) {
    return HAL_EINVAL;
  }
  if (!ota_ensure_mutex()) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(s_ota.mutex);
  jh_ota_boot_state_t state{};
  const hal_status_t status = jh_rp_ota_storage_get_state(&state);
  hal_mutex_unlock(s_ota.mutex);
  if (status != HAL_OK) {
    return status;
  }
  memset(out_info, 0, sizeof(*out_info));
  out_info->mode = (hal_ota_boot_mode_t)state.mode;
  out_info->attempts = state.attempts;
  out_info->max_attempts = state.max_attempts;
  out_info->program_generation = state.program_generation;
  out_info->staging_generation = state.staging_generation;
  memcpy(out_info->program_version, state.program_version,
         sizeof(out_info->program_version));
  memcpy(out_info->staging_version, state.staging_version,
         sizeof(out_info->staging_version));
  return HAL_OK;
}

#endif /* HAL_ENABLE_OTA */
#endif // HAL_TARGET_IS_RP
