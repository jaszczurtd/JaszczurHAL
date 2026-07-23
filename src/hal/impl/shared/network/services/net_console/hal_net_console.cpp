/** @file Target-neutral network console service over HAL TCP. */
#include "hal/hal_net_console.h"

#ifdef HAL_ENABLE_NET_CONSOLE

#include "hal/hal_tcp.h"

#include <string.h>

namespace {

enum client_state_t : uint8_t {
  CLIENT_UNUSED = 0u,
  CLIENT_AUTH,
  CLIENT_OPEN,
  CLIENT_CLOSING
};

struct byte_ring_t {
  uint8_t *data;
  size_t size;
  size_t head;
  size_t tail;
};

struct net_console_client_t {
  hal_tcp_socket_t socket;
  hal_net_endpoint_t remote;
  client_state_t state;
  char auth_line[HAL_NET_CONSOLE_PASSWORD_MAX];
  size_t auth_len;
  char line[HAL_NET_CONSOLE_LINE_BUFFER_SIZE];
  size_t line_len;
  uint8_t tx_storage[HAL_NET_CONSOLE_TX_BUFFER_SIZE];
  byte_ring_t tx;
};

hal_tcp_listener_t s_listener = NULL;
net_console_client_t s_clients[HAL_NET_CONSOLE_MAX_CLIENTS] = {};
char s_password[HAL_NET_CONSOLE_PASSWORD_MAX] = {};
hal_net_console_event_cb_t s_event_cb = NULL;
hal_net_console_line_cb_t s_line_cb = NULL;
void *s_cb_user = NULL;
uint8_t s_rx_storage[HAL_NET_CONSOLE_RX_BUFFER_SIZE] = {};
byte_ring_t s_rx = {s_rx_storage, sizeof(s_rx_storage), 0u, 0u};

const char kGreeting[] = "\r\nJaszczurHAL net console\r\nPassword: ";
const char kAuthOk[] = "\r\nOK\r\n> ";
const char kAuthFail[] = "\r\nERR auth\r\n";
const char kLineTooLong[] = "\r\nERR line too long\r\n";
const char kBusy[] = "\r\nERR busy\r\n";

void ring_init(byte_ring_t *ring, uint8_t *storage, size_t size) {
  ring->data = storage;
  ring->size = size;
  ring->head = 0u;
  ring->tail = 0u;
}

bool ring_empty(const byte_ring_t *ring) { return ring->head == ring->tail; }

size_t ring_available(const byte_ring_t *ring) {
  if (ring->head >= ring->tail) {
    return ring->head - ring->tail;
  }
  return ring->size - ring->tail + ring->head;
}

bool ring_push(byte_ring_t *ring, uint8_t value) {
  size_t next = (ring->head + 1u) % ring->size;
  if (next == ring->tail) {
    return false;
  }
  ring->data[ring->head] = value;
  ring->head = next;
  return true;
}

bool ring_pop(byte_ring_t *ring, uint8_t *value) {
  if (ring_empty(ring)) {
    return false;
  }
  *value = ring->data[ring->tail];
  ring->tail = (ring->tail + 1u) % ring->size;
  return true;
}

size_t ring_contiguous_read(const byte_ring_t *ring) {
  if (ring_empty(ring)) {
    return 0u;
  }
  if (ring->head > ring->tail) {
    return ring->head - ring->tail;
  }
  return ring->size - ring->tail;
}

void ring_drop(byte_ring_t *ring, size_t len) {
  size_t available = ring_available(ring);
  if (len > available) {
    len = available;
  }
  ring->tail = (ring->tail + len) % ring->size;
}

hal_status_t enqueue_bytes(byte_ring_t *ring, const void *data, size_t len) {
  if (!data && len > 0u) {
    return HAL_EINVAL;
  }
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  hal_status_t status = HAL_OK;
  for (size_t i = 0u; i < len; ++i) {
    if (!ring_push(ring, bytes[i])) {
      status = HAL_EOVERFLOW;
      break;
    }
  }
  return status;
}

void clear_client(net_console_client_t *client) {
  if (client->socket) {
    hal_tcp_socket_close(client->socket);
  }
  client->socket = NULL;
  memset(&client->remote, 0, sizeof(client->remote));
  client->state = CLIENT_UNUSED;
  client->auth_len = 0u;
  client->line_len = 0u;
  memset(client->auth_line, 0, sizeof(client->auth_line));
  memset(client->line, 0, sizeof(client->line));
  ring_init(&client->tx, client->tx_storage, sizeof(client->tx_storage));
}

bool client_index_valid(hal_net_console_client_t client) {
  return client < HAL_NET_CONSOLE_MAX_CLIENTS;
}

hal_net_console_client_t client_index(const net_console_client_t *client) {
  return static_cast<hal_net_console_client_t>(client - s_clients);
}

void emit_event(net_console_client_t *client, hal_net_console_event_t event) {
  if (s_event_cb) {
    s_event_cb(client_index(client), event, s_cb_user);
  }
}

void close_client(net_console_client_t *client, bool emit_disconnect) {
  bool was_active = client->state != CLIENT_UNUSED;
  if (emit_disconnect && was_active) {
    emit_event(client, HAL_NET_CONSOLE_EVENT_DISCONNECT);
  }
  clear_client(client);
}

hal_status_t enqueue_to_client(net_console_client_t *client, const void *data,
                               size_t len) {
  if (!client || client->state == CLIENT_UNUSED) {
    return HAL_ENOENT;
  }
  return enqueue_bytes(&client->tx, data, len);
}

void enqueue_text_to_client(net_console_client_t *client, const char *text) {
  if (text) {
    (void)enqueue_to_client(client, text, strlen(text));
  }
}

net_console_client_t *find_free_client(void) {
  for (size_t i = 0u; i < HAL_NET_CONSOLE_MAX_CLIENTS; ++i) {
    if (s_clients[i].state == CLIENT_UNUSED) {
      return &s_clients[i];
    }
  }
  return NULL;
}

void accept_pending_clients(void) {
  while (hal_tcp_listener_can_accept(s_listener)) {
    hal_net_endpoint_t remote = {};
    hal_tcp_socket_t socket = hal_tcp_listener_accept(s_listener, &remote, 0u);
    if (!socket) {
      return;
    }

    net_console_client_t *slot = find_free_client();
    if (!slot) {
      (void)hal_tcp_socket_send(socket, kBusy, strlen(kBusy));
      hal_tcp_socket_close(socket);
      continue;
    }

    clear_client(slot);
    slot->socket = socket;
    slot->remote = remote;
    slot->state = CLIENT_AUTH;
    enqueue_text_to_client(slot, kGreeting);
    emit_event(slot, HAL_NET_CONSOLE_EVENT_CONNECT);
  }
}

void flush_client_tx(net_console_client_t *client) {
  if (client->state == CLIENT_UNUSED || !client->socket) {
    return;
  }
  while (!ring_empty(&client->tx)) {
    size_t chunk = ring_contiguous_read(&client->tx);
    if (chunk == 0u) {
      return;
    }
    int sent = hal_tcp_socket_send(client->socket,
                                   client->tx.data + client->tx.tail, chunk);
    if (sent <= 0) {
      close_client(client, true);
      return;
    }
    ring_drop(&client->tx, static_cast<size_t>(sent));
    if (static_cast<size_t>(sent) < chunk) {
      return;
    }
  }
}

void authenticate_byte(net_console_client_t *client, uint8_t byte) {
  if (byte == '\r') {
    return;
  }
  if (byte != '\n') {
    if (client->auth_len + 1u >= sizeof(client->auth_line)) {
      enqueue_text_to_client(client, kAuthFail);
      client->state = CLIENT_CLOSING;
      return;
    }
    client->auth_line[client->auth_len++] = static_cast<char>(byte);
    client->auth_line[client->auth_len] = '\0';
    return;
  }

  if (strcmp(client->auth_line, s_password) == 0) {
    client->auth_len = 0u;
    client->auth_line[0] = '\0';
    client->state = CLIENT_OPEN;
    enqueue_text_to_client(client, kAuthOk);
    emit_event(client, HAL_NET_CONSOLE_EVENT_AUTHENTICATED);
  } else {
    enqueue_text_to_client(client, kAuthFail);
    client->state = CLIENT_CLOSING;
  }
}

void deliver_line(net_console_client_t *client) {
  client->line[client->line_len] = '\0';
  if (s_line_cb) {
    hal_status_t status =
        s_line_cb(client_index(client), client->line, s_cb_user);
    (void)status;
  }
  client->line_len = 0u;
  client->line[0] = '\0';
}

void process_open_byte(net_console_client_t *client, uint8_t byte) {
  (void)ring_push(&s_rx, byte);

  if (byte == '\r') {
    return;
  }
  if (byte == '\n') {
    deliver_line(client);
    return;
  }
  if (client->line_len + 1u >= sizeof(client->line)) {
    client->line_len = 0u;
    client->line[0] = '\0';
    enqueue_text_to_client(client, kLineTooLong);
    return;
  }
  client->line[client->line_len++] = static_cast<char>(byte);
  client->line[client->line_len] = '\0';
}

void process_client_rx(net_console_client_t *client) {
  if (client->state == CLIENT_UNUSED || client->state == CLIENT_CLOSING ||
      !client->socket) {
    return;
  }
  if (!hal_tcp_socket_is_connected(client->socket)) {
    close_client(client, true);
    return;
  }

  uint8_t buffer[64];
  for (;;) {
    int read = hal_tcp_socket_recv(client->socket, buffer, sizeof(buffer), 0u);
    if (read < 0) {
      close_client(client, true);
      return;
    }
    if (read == 0) {
      return;
    }
    for (int i = 0; i < read; ++i) {
      if (client->state == CLIENT_AUTH) {
        authenticate_byte(client, buffer[i]);
      } else if (client->state == CLIENT_OPEN) {
        process_open_byte(client, buffer[i]);
      }
      if (client->state == CLIENT_CLOSING) {
        return;
      }
    }
  }
}

hal_status_t broadcast_to_open_clients(const void *data, size_t len,
                                       bool allow_empty) {
  if (!data && len > 0u) {
    return HAL_EINVAL;
  }
  if (len == 0u) {
    return HAL_OK;
  }

  size_t sent_clients = 0u;
  hal_status_t status = HAL_OK;
  for (size_t i = 0u; i < HAL_NET_CONSOLE_MAX_CLIENTS; ++i) {
    if (s_clients[i].state != CLIENT_OPEN) {
      continue;
    }
    ++sent_clients;
    hal_status_t client_status = enqueue_to_client(&s_clients[i], data, len);
    if (client_status != HAL_OK) {
      status = client_status;
    }
  }

  if (sent_clients == 0u && !allow_empty) {
    return HAL_ENOENT;
  }
  return status;
}

} // namespace

extern "C" hal_status_t
hal_net_console_set_callbacks(hal_net_console_event_cb_t event_cb,
                              hal_net_console_line_cb_t line_cb, void *user) {
  s_event_cb = event_cb;
  s_line_cb = line_cb;
  s_cb_user = user;
  return HAL_OK;
}

extern "C" hal_status_t hal_net_console_start(uint16_t port,
                                              const char *password) {
  if (s_listener) {
    return HAL_OK;
  }
  if (port == 0u || !password || password[0] == '\0') {
    return HAL_EINVAL;
  }
  size_t password_len = strlen(password);
  if (password_len >= sizeof(s_password)) {
    return HAL_EOVERFLOW;
  }

  for (size_t i = 0u; i < HAL_NET_CONSOLE_MAX_CLIENTS; ++i) {
    clear_client(&s_clients[i]);
  }
  ring_init(&s_rx, s_rx_storage, sizeof(s_rx_storage));
  memcpy(s_password, password, password_len);
  s_password[password_len] = '\0';

  s_listener = hal_tcp_listener_open();
  if (!s_listener) {
    s_password[0] = '\0';
    return HAL_ENOMEM;
  }

  hal_net_endpoint_t local = {};
  local.family = HAL_NET_AF_INET;
  local.addr_len = HAL_NET_IPV4_ADDR_LEN;
  local.port = port;

  if (!hal_tcp_listener_bind(s_listener, &local) ||
      !hal_tcp_listener_listen(s_listener, HAL_NET_CONSOLE_DEFAULT_BACKLOG)) {
    hal_tcp_listener_close(s_listener);
    s_listener = NULL;
    s_password[0] = '\0';
    return HAL_EIO;
  }

  return HAL_OK;
}

extern "C" void hal_net_console_stop(void) {
  for (size_t i = 0u; i < HAL_NET_CONSOLE_MAX_CLIENTS; ++i) {
    clear_client(&s_clients[i]);
  }
  if (s_listener) {
    hal_tcp_listener_close(s_listener);
    s_listener = NULL;
  }
  s_password[0] = '\0';
  ring_init(&s_rx, s_rx_storage, sizeof(s_rx_storage));
}

extern "C" bool hal_net_console_is_running(void) { return s_listener != NULL; }

extern "C" void hal_net_console_poll(void) {
  if (!s_listener) {
    return;
  }

  accept_pending_clients();

  for (size_t i = 0u; i < HAL_NET_CONSOLE_MAX_CLIENTS; ++i) {
    net_console_client_t *client = &s_clients[i];
    if (client->state == CLIENT_UNUSED) {
      continue;
    }

    process_client_rx(client);
    if (client->state == CLIENT_UNUSED) {
      continue;
    }
    flush_client_tx(client);
    if (client->state == CLIENT_CLOSING && ring_empty(&client->tx)) {
      close_client(client, true);
    }
  }
}

extern "C" size_t hal_net_console_client_count(void) {
  size_t count = 0u;
  for (size_t i = 0u; i < HAL_NET_CONSOLE_MAX_CLIENTS; ++i) {
    if (s_clients[i].state != CLIENT_UNUSED) {
      ++count;
    }
  }
  return count;
}

extern "C" size_t hal_net_console_authenticated_count(void) {
  size_t count = 0u;
  for (size_t i = 0u; i < HAL_NET_CONSOLE_MAX_CLIENTS; ++i) {
    if (s_clients[i].state == CLIENT_OPEN) {
      ++count;
    }
  }
  return count;
}

extern "C" bool
hal_net_console_client_is_authenticated(hal_net_console_client_t client) {
  return client_index_valid(client) && s_clients[client].state == CLIENT_OPEN;
}

extern "C" hal_status_t hal_net_console_write(const void *data, size_t len) {
  return broadcast_to_open_clients(data, len, false);
}

extern "C" hal_status_t hal_net_console_write_text(const char *text) {
  if (!text) {
    return HAL_EINVAL;
  }
  return hal_net_console_write(text, strlen(text));
}

extern "C" hal_status_t
hal_net_console_write_to(hal_net_console_client_t client, const void *data,
                         size_t len) {
  if (!client_index_valid(client) || (!data && len > 0u)) {
    return HAL_EINVAL;
  }
  if (s_clients[client].state != CLIENT_OPEN) {
    return HAL_ENOENT;
  }
  return enqueue_to_client(&s_clients[client], data, len);
}

extern "C" hal_status_t
hal_net_console_write_text_to(hal_net_console_client_t client,
                              const char *text) {
  if (!text) {
    return HAL_EINVAL;
  }
  return hal_net_console_write_to(client, text, strlen(text));
}

extern "C" int hal_net_console_available(void) {
  return static_cast<int>(ring_available(&s_rx));
}

extern "C" int hal_net_console_read(void *buffer, size_t max_len) {
  if (!buffer && max_len > 0u) {
    return -1;
  }
  uint8_t *out = static_cast<uint8_t *>(buffer);
  size_t read = 0u;
  while (read < max_len) {
    uint8_t value = 0u;
    if (!ring_pop(&s_rx, &value)) {
      break;
    }
    out[read++] = value;
  }
  return static_cast<int>(read);
}

extern "C" void hal_net_console_close(hal_net_console_client_t client) {
  if (client_index_valid(client)) {
    close_client(&s_clients[client], true);
  }
}

extern "C" void hal_net_console_write_from_serial(const char *data,
                                                  size_t len) {
  (void)broadcast_to_open_clients(data, len, true);
}

#endif /* HAL_ENABLE_NET_CONSOLE */
