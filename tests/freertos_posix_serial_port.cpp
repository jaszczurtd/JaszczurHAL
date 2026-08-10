#include "freertos_posix_serial_port.h"

#include "hal/impl/shared/debug/jh_serial_port.h"

#include <string.h>

namespace {

constexpr size_t kCaptureSize = 16384u;
char s_capture[kCaptureSize] = {};
size_t s_capture_len = 0u;
bool s_capture_overflowed = false;

void capture_append(const char *data, size_t len) {
  if (data == NULL || len == 0u) {
    return;
  }

  const size_t room = (kCaptureSize - 1u) - s_capture_len;
  const size_t copy_len = len < room ? len : room;
  if (copy_len > 0u) {
    memcpy(s_capture + s_capture_len, data, copy_len);
    s_capture_len += copy_len;
    s_capture[s_capture_len] = '\0';
  }
  if (copy_len != len) {
    s_capture_overflowed = true;
  }
}

} // namespace

extern "C" void jh_serial_port_begin(uint32_t baud) { (void)baud; }

extern "C" void jh_serial_port_set_flush(bool enabled) { (void)enabled; }

extern "C" void jh_serial_port_message_begin(jh_serial_port_message_t kind) {
  (void)kind;
}

extern "C" void jh_serial_port_write(const char *data, size_t len) {
  capture_append(data, len);
}

extern "C" size_t jh_serial_port_finish_line(char line_ending[2]) {
  line_ending[0] = '\n';
  capture_append(line_ending, 1u);
  return 1u;
}

extern "C" void jh_serial_port_flush(void) {}

extern "C" int jh_serial_port_available(void) { return 0; }

extern "C" int jh_serial_port_read(void) { return -1; }

extern "C" void jh_test_serial_capture_reset(void) {
  s_capture[0] = '\0';
  s_capture_len = 0u;
  s_capture_overflowed = false;
}

extern "C" const char *jh_test_serial_capture_data(void) { return s_capture; }

extern "C" size_t jh_test_serial_capture_size(void) { return s_capture_len; }

extern "C" bool jh_test_serial_capture_overflowed(void) {
  return s_capture_overflowed;
}
