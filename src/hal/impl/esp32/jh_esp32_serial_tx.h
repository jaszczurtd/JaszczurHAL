#pragma once

#include <driver/usb_serial_jtag.h>
#include <stddef.h>

/** @brief Submit a chunk to the atomic IDF TX queue without waiting.
 * @param data Bytes to send; valid for length bytes.
 * @param length Available bytes; zero performs no driver call.
 * @return Bytes accepted, zero for backpressure, or the driver error.
 * @note IDF queues a complete write or none of it. Its installed TX buffer may
 * be smaller than our record (including a driver installed by the application).
 * Decrease the attempted chunk without waiting; at most ten zero-timeout
 * attempts reach one byte from the initial 512-byte bound. */
static inline int jh_esp32_usb_try_write(const void *data, size_t length) {
  size_t chunk = length < 512U ? length : 512U;
  if (chunk == 0U) {
    return 0;
  }
  for (;;) {
    const int written = usb_serial_jtag_write_bytes(data, chunk, 0U);
    if (written != 0 || chunk == 1U) {
      return written;
    }
    chunk /= 2U;
  }
}
