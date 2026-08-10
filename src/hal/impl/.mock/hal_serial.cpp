#include "../../hal_target.h"
#if HAL_TARGET_IS_MOCK

#include "../../hal_serial.h"
#include "../shared/debug/jh_serial_port.h"
#include "hal_mock.h"

#include <stdio.h>
#include <string.h>

static char s_last_serial_line[HAL_DEBUG_BUF_SIZE] = {};
static char s_last_deb_line[HAL_DEBUG_BUF_SIZE] = {};
static unsigned char s_mock_rx_buf[HAL_DEBUG_BUF_SIZE] = {};
static int s_mock_rx_len = 0;
static int s_mock_rx_pos = 0;
static bool s_capture_debug = false;

static void mock_capture_clear(char *dst, size_t dst_size) {
  if (dst_size > 0u) {
    dst[0] = '\0';
  }
}

static void mock_capture_append(char *dst, size_t dst_size, const char *data,
                                size_t len) {
  if (dst_size == 0u || data == NULL || len == 0u) {
    return;
  }

  size_t used = strlen(dst);
  if (used >= dst_size - 1u) {
    return;
  }

  const size_t room = dst_size - 1u - used;
  const size_t copy_len = len < room ? len : room;
  memcpy(dst + used, data, copy_len);
  dst[used + copy_len] = '\0';
}

void jh_serial_port_begin(uint32_t baud) { (void)baud; }

void jh_serial_port_set_flush(bool enabled) { (void)enabled; }

void jh_serial_port_message_begin(jh_serial_port_message_t kind) {
  if (kind != JH_SERIAL_PORT_MESSAGE_APPEND) {
    mock_capture_clear(s_last_serial_line, sizeof(s_last_serial_line));
  }
  if (kind == JH_SERIAL_PORT_MESSAGE_DEBUG) {
    mock_capture_clear(s_last_deb_line, sizeof(s_last_deb_line));
  }
  s_capture_debug = kind == JH_SERIAL_PORT_MESSAGE_DEBUG;
}

void jh_serial_port_write(const char *data, size_t len) {
  if (data == NULL || len == 0u) {
    return;
  }

  (void)fwrite(data, 1u, len, stdout);
  mock_capture_append(s_last_serial_line, sizeof(s_last_serial_line), data,
                      len);
  if (s_capture_debug) {
    mock_capture_append(s_last_deb_line, sizeof(s_last_deb_line), data, len);
  }
}

size_t jh_serial_port_finish_line(char line_ending[2]) {
  line_ending[0] = '\n';
  (void)fputc('\n', stdout);
  return 1u;
}

void jh_serial_port_flush(void) {}

int jh_serial_port_available(void) { return s_mock_rx_len - s_mock_rx_pos; }

int jh_serial_port_read(void) {
  if (s_mock_rx_pos >= s_mock_rx_len) {
    return -1;
  }
  return (int)s_mock_rx_buf[s_mock_rx_pos++];
}

const char *hal_mock_serial_last_line(void) { return s_last_serial_line; }

const char *hal_mock_deb_last_line(void) { return s_last_deb_line; }

void hal_mock_serial_reset(void) {
  mock_capture_clear(s_last_serial_line, sizeof(s_last_serial_line));
  mock_capture_clear(s_last_deb_line, sizeof(s_last_deb_line));
  s_mock_rx_pos = 0;
  s_mock_rx_len = 0;
  s_capture_debug = false;
}

void hal_mock_serial_inject_rx(const char *data, int len) {
  if (data == NULL) {
    s_mock_rx_len = 0;
    s_mock_rx_pos = 0;
    return;
  }
  if (len < 0) {
    len = (int)strlen(data);
  }
  if (len > (int)sizeof(s_mock_rx_buf)) {
    len = (int)sizeof(s_mock_rx_buf);
  }
  memcpy(s_mock_rx_buf, data, (size_t)len);
  s_mock_rx_len = len;
  s_mock_rx_pos = 0;
}

#endif // HAL_TARGET_IS_MOCK
