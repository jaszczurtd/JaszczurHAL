#include <hal/hal_app.h>
#include <hal/hal_net_console.h>
#include <hal/hal_system.h>
#include <hal/hal_wifi.h>
#include <stdio.h>
#include <string.h>
#include <tools_c.h>

static const char *WIFI_SSID = "your-ssid";
static const char *WIFI_PASSWORD = "your-password";
static const char *CONSOLE_PASSWORD = "change-me";

static uint32_t last_wifi_check_ms = 0;
static uint32_t last_status_ms = 0;
static uint32_t last_log_ms = 0;
static bool console_started = false;
static uint32_t command_count = 0;

static void on_console_event(hal_net_console_client_t client,
                             hal_net_console_event_t event, void *user) {
  (void)user;
  if (event == HAL_NET_CONSOLE_EVENT_CONNECT) {
    deb("NETCON: client %u connected", (unsigned)client);
  } else if (event == HAL_NET_CONSOLE_EVENT_AUTHENTICATED) {
    deb("NETCON: client %u authenticated", (unsigned)client);
  } else if (event == HAL_NET_CONSOLE_EVENT_DISCONNECT) {
    deb("NETCON: client %u disconnected", (unsigned)client);
  }
}

static hal_status_t on_console_line(hal_net_console_client_t client,
                                    const char *line, void *user) {
  (void)user;
  command_count++;

  if (strcmp(line, "help") == 0) {
    return hal_net_console_write_text_to(
        client, "commands: help, status, clients, echo <text>\r\n");
  }

  if (strcmp(line, "status") == 0) {
    char ip[32] = {0};
    char reply[160] = {0};
    hal_wifi_get_local_ip(ip, sizeof(ip));
    snprintf(reply, sizeof(reply),
             "uptime=%lu ms ip=%s rssi=%ld commands=%lu\r\n",
             (unsigned long)hal_millis(), ip, (long)hal_wifi_rssi(),
             (unsigned long)command_count);
    return hal_net_console_write_text_to(client, reply);
  }

  if (strcmp(line, "clients") == 0) {
    char reply[96] = {0};
    snprintf(reply, sizeof(reply), "clients=%lu authenticated=%lu\r\n",
             (unsigned long)hal_net_console_client_count(),
             (unsigned long)hal_net_console_authenticated_count());
    return hal_net_console_write_text_to(client, reply);
  }

  if (strncmp(line, "echo ", 5u) == 0) {
    hal_status_t status = hal_net_console_write_text_to(client, line + 5u);
    if (status != HAL_OK) {
      return status;
    }
    return hal_net_console_write_text_to(client, "\r\n");
  }

  return hal_net_console_write_text_to(client, "unknown command\r\n");
}

static void connect_wifi(void) {
  if (hal_wifi_is_connected()) {
    return;
  }

  const uint32_t now = hal_millis();
  if (now - last_wifi_check_ms < 5000u) {
    return;
  }
  last_wifi_check_ms = now;

  deb("WiFi: connecting to %s", WIFI_SSID);
  hal_wifi_set_mode(HAL_WIFI_MODE_STA);
  hal_wifi_set_hostname("jaszczurhal-netcon");
  hal_wifi_begin_station(WIFI_SSID, WIFI_PASSWORD, true);
}

static void start_console_when_ready(void) {
  if (console_started || !hal_wifi_is_connected()) {
    return;
  }

  hal_net_console_set_callbacks(on_console_event, on_console_line, NULL);
  hal_status_t status =
      hal_net_console_start(HAL_NET_CONSOLE_DEFAULT_PORT, CONSOLE_PASSWORD);
  console_started = status == HAL_OK;

  if (console_started) {
    char ip[32] = {0};
    hal_wifi_get_local_ip(ip, sizeof(ip));
    deb("NETCON: listening on %s:%u", ip,
        (unsigned)HAL_NET_CONSOLE_DEFAULT_PORT);
  } else {
    derr("NETCON: failed to start, status=%s", hal_status_to_string(status));
  }
}

static void print_status(void) {
  const uint32_t now = hal_millis();
  if (now - last_status_ms < 5000u) {
    return;
  }
  last_status_ms = now;

  if (!hal_wifi_is_connected()) {
    deb("WiFi: disconnected, status=%d", hal_wifi_status());
    return;
  }

  deb("NETCON: running=%d clients=%lu authenticated=%lu commands=%lu",
      hal_net_console_is_running() ? 1 : 0,
      (unsigned long)hal_net_console_client_count(),
      (unsigned long)hal_net_console_authenticated_count(),
      (unsigned long)command_count);
}

static void emit_periodic_log(void) {
  const uint32_t now = hal_millis();
  if (now - last_log_ms < 1000u) {
    return;
  }
  last_log_ms = now;
  deb("NETCON: heartbeat uptime=%lu", (unsigned long)now);
}

void app_start(void) { debugInit(); }

void app_task0(void) {
  connect_wifi();
  start_console_when_ready();
  hal_net_console_poll();
  emit_periodic_log();
  print_status();
}
