#include "device_profile.h"

#include <hal/core/hal_app.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <tools_c.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { JH_C85_DEVICE_COMMAND_CAPACITY = 16u };

static hal_status_t s_status = HAL_NONE;
static char s_command[JH_C85_DEVICE_COMMAND_CAPACITY];
static size_t s_command_length;

static void execute_command(void) {
  s_command[s_command_length] = '\0';
  if (strcmp(s_command, "INFO") == 0) {
    bool controller_ready = false;
    bool hid_connected = false;
    uint32_t report_count = 0u;
    jh_c85_hid_device_get_info(&controller_ready, &hid_connected,
                               &report_count);
    deb("JHC85-DEVICE-INFO status=%s controller=%u hid=%u reports=%u",
        hal_status_to_string(s_status), controller_ready ? 1u : 0u,
        hid_connected ? 1u : 0u, (unsigned)report_count);
  } else {
    deb("JHC85-DEVICE command %s: HAL_EINVAL", s_command);
  }
  s_command_length = 0u;
}

static void service_commands(void) {
  while (hal_serial_available() > 0) {
    const int value = hal_serial_read();
    if (value < 0) {
      return;
    }
    if (value == '\r') {
      continue;
    }
    if (value == '\n') {
      if (s_command_length != 0u) {
        execute_command();
      }
      continue;
    }
    if (s_command_length + 1u >= sizeof(s_command)) {
      s_command_length = 0u;
      continue;
    }
    s_command[s_command_length++] = (char)value;
  }
}

void app_start(void) {
  debugInit();
  s_status = jh_c85_hid_device_start();
  deb("JHC85-DEVICE start: %s", hal_status_to_string(s_status));
  deb("JHC85-DEVICE command: INFO");
}

void app_task0(void) {
  service_commands();
  if (s_status == HAL_OK) {
    s_status = jh_c85_hid_device_service();
    if (s_status != HAL_OK) {
      derr("JHC85-DEVICE service failed: %s", hal_status_to_string(s_status));
    }
  }
  hal_delay_ms(1u);
}
