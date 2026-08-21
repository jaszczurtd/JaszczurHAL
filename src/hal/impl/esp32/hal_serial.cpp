#include "hal/core/hal_target.h"
#if HAL_TARGET_IS_ESP32_FAMILY

#include "hal/core/hal_config.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/debug/jh_serial_port.h"
#include "hal/system/hal_sync.h"

#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <sdkconfig.h>

#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#if !defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) ||                            \
    !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#error "JaszczurHAL: ESP32 serial requires the USB Serial/JTAG console VFS."
#endif

namespace {

constexpr size_t kRxCapacity = 256u;
constexpr uint32_t kDriverTxCapacity = 512u;
constexpr uint32_t kDriverRxCapacity = 1024u;

hal_mutex_t s_control_mutex;
hal_mutex_t s_rx_mutex;
volatile bool s_started = false;
volatile bool s_flush_enabled = false;
uint8_t s_rx_buffer[kRxCapacity] = {};
size_t s_rx_head = 0u;
size_t s_rx_tail = 0u;
size_t s_rx_count = 0u;

bool ensure_started(void) {
  if (__atomic_load_n(&s_started, __ATOMIC_ACQUIRE)) {
    return true;
  }
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_control_mutex);
  if (mutex == nullptr) {
    return false;
  }

  hal_mutex_lock(mutex);
  if (!__atomic_load_n(&s_started, __ATOMIC_ACQUIRE)) {
    esp_err_t driver_result = ESP_OK;
    if (!usb_serial_jtag_is_driver_installed()) {
      usb_serial_jtag_driver_config_t config = {
          .tx_buffer_size = kDriverTxCapacity,
          .rx_buffer_size = kDriverRxCapacity,
      };
      driver_result = usb_serial_jtag_driver_install(&config);
      /* Another initializer may have won the race outside the HAL mutex. In
       * that case adopt the one installed driver instead of creating another
       * peripheral/VFS owner. */
      if (driver_result == ESP_ERR_INVALID_STATE &&
          usb_serial_jtag_is_driver_installed()) {
        driver_result = ESP_OK;
      }
    }

    if (driver_result == ESP_OK) {
      /* Startup already registered /dev/usbserjtag and connected the console
       * VFS. Switch that existing VFS to the official interrupt-driven driver;
       * do not register a second VFS instance. */
      usb_serial_jtag_vfs_use_driver();
    }

    const int flags =
        driver_result == ESP_OK ? fcntl(STDIN_FILENO, F_GETFL, 0) : -1;
    const int nonblocking_result =
        flags >= 0 ? fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) : -1;
    if (nonblocking_result >= 0) {
      __atomic_store_n(&s_started, true, __ATOMIC_RELEASE);
    }
  }
  const bool started = __atomic_load_n(&s_started, __ATOMIC_ACQUIRE);
  hal_mutex_unlock(mutex);
  return started;
}

void pump_rx_locked(void) {
  while (s_rx_count < kRxCapacity) {
    uint8_t chunk[64] = {};
    const size_t free_bytes = kRxCapacity - s_rx_count;
    const size_t requested =
        free_bytes < sizeof(chunk) ? free_bytes : sizeof(chunk);
    const ssize_t received = read(STDIN_FILENO, chunk, requested);
    if (received <= 0) {
      return;
    }

    for (ssize_t index = 0; index < received; ++index) {
      s_rx_buffer[s_rx_head] = chunk[index];
      s_rx_head = (s_rx_head + 1u) % kRxCapacity;
      ++s_rx_count;
    }
    if ((size_t)received < requested) {
      return;
    }
  }
}

hal_mutex_t rx_mutex(void) { return jh_hal_mutex_create_once(&s_rx_mutex); }

} // namespace

void jh_serial_port_begin(uint32_t baud) {
  (void)baud;
  const bool started = ensure_started();
  HAL_ASSERT(started, "jh_serial_port_begin: console VFS setup failed");
}

void jh_serial_port_set_flush(bool enabled) {
  __atomic_store_n(&s_flush_enabled, enabled, __ATOMIC_RELEASE);
}

void jh_serial_port_message_begin(jh_serial_port_message_t kind) { (void)kind; }

void jh_serial_port_write(const char *data, size_t len) {
  if (data == nullptr || len == 0u || !ensure_started()) {
    return;
  }

  size_t offset = 0u;
  while (offset < len) {
    const ssize_t written = write(STDOUT_FILENO, data + offset, len - offset);
    if (written <= 0) {
      return;
    }
    offset += (size_t)written;
  }
}

size_t jh_serial_port_finish_line(char line_ending[2]) {
  if (line_ending == nullptr) {
    return 0u;
  }
  line_ending[0] = '\n';
  jh_serial_port_write(line_ending, 1u);
  return 1u;
}

void jh_serial_port_flush(void) {
  if (__atomic_load_n(&s_flush_enabled, __ATOMIC_ACQUIRE) && ensure_started()) {
    (void)fsync(STDOUT_FILENO);
  }
}

int jh_serial_port_available(void) {
  if (!ensure_started()) {
    return 0;
  }
  hal_mutex_t mutex = rx_mutex();
  if (mutex == nullptr) {
    return 0;
  }

  hal_mutex_lock(mutex);
  pump_rx_locked();
  const int available =
      s_rx_count > (size_t)INT_MAX ? INT_MAX : (int)s_rx_count;
  hal_mutex_unlock(mutex);
  return available;
}

int jh_serial_port_read(void) {
  if (!ensure_started()) {
    return -1;
  }
  hal_mutex_t mutex = rx_mutex();
  if (mutex == nullptr) {
    return -1;
  }

  hal_mutex_lock(mutex);
  pump_rx_locked();
  if (s_rx_count == 0u) {
    hal_mutex_unlock(mutex);
    return -1;
  }
  const int value = (int)s_rx_buffer[s_rx_tail];
  s_rx_tail = (s_rx_tail + 1u) % kRxCapacity;
  --s_rx_count;
  hal_mutex_unlock(mutex);
  return value;
}

#endif // HAL_TARGET_IS_ESP32_FAMILY
