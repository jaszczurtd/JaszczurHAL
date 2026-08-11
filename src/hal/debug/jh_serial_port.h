#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file jh_serial_port.h
 * @brief Internal link-time transport contract for the shared serial/debug
 * core.
 *
 * Exactly one target port implements these operations in a firmware or host
 * build. All calls except begin, configuration, and RX are serialized by the
 * shared core's TX mutex. The port owns only byte transport, target line
 * endings, optional flush behavior, and target test capture/RX facilities.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Logical message kinds used by transports with test capture facilities. */
typedef enum {
  JH_SERIAL_PORT_MESSAGE_APPEND = 0,
  JH_SERIAL_PORT_MESSAGE_LINE,
  JH_SERIAL_PORT_MESSAGE_DEBUG,
  JH_SERIAL_PORT_MESSAGE_ERROR,
} jh_serial_port_message_t;

/** Initialise the target transport for the requested baud rate. */
void jh_serial_port_begin(uint32_t baud);

/** Configure target-specific eager flushing, if the transport supports it. */
void jh_serial_port_set_flush(bool enabled);

/**
 * Start one atomic serial message.
 *
 * Hardware ports ignore the kind. The mock port uses it to preserve append
 * semantics, reset its last-message capture, and mirror debug-message bytes
 * into the capture returned by hal_mock_deb_last_line().
 */
void jh_serial_port_message_begin(jh_serial_port_message_t kind);

/** Write bytes to the target transport. */
void jh_serial_port_write(const char *data, size_t len);

/**
 * Emit the target line ending and return its bytes for net-console mirroring.
 *
 * @param[out] line_ending Storage for up to two line-ending bytes.
 * @return Number of valid bytes stored in @p line_ending (zero to two).
 */
size_t jh_serial_port_finish_line(char line_ending[2]);

/** Flush pending target bytes when eager flushing is enabled. */
void jh_serial_port_flush(void);

/** Return the number of immediately readable RX bytes. */
int jh_serial_port_available(void);

/** Return one unsigned RX byte, or -1 when no byte is available. */
int jh_serial_port_read(void);

#ifdef __cplusplus
}
#endif
