#ifndef JH_FIRMWARE_READ_H
#define JH_FIRMWARE_READ_H

/** @file jh_firmware_read.h
 * Build-selected flash reads on the host. No operation writes device flash.
 * Paths and selectors are UTF-8; callers serialize access to a device/build.
 */
#include "jh_json_file.h"
#include <jb/process.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Borrowed invocation inputs; zero-init, then set project, assembly and output
 * to nonempty strings. build_directory and tool may be NULL (automatic choice).
 * variant NULL selects the base build. serial is required for RP/STM, port for
 * ESP. address and size must both be NULL (default region) or decimal/0x text.
 * reset permits entering BOOTSEL/ROM or resetting the selected target.
 */
typedef struct {
  const char *project, *assembly, *variant, *build_directory, *output;
  const char *serial, *port, *tool, *address, *size;
  bool reset;
  uint32_t timeout_ms;      /**< Zero waits indefinitely. */
  jb_process_output_fn log; /**< Optional tool output consumer. */
  jb_cancel_fn canceled; /**< Optional cancellation, also before publishing. */
  void *context;         /**< Borrowed by callbacks. */
  uint32_t
      baud_rate; /**< ESP UART baud; zero uses 115200. Invalid on RP/STM. */
  bool automatic_timeout; /**< Derive timeout from range and baud instead of
                             timeout_ms. */
} jh_firmware_options_t;

/** Result, also filled on failure. device_state describes the tool's completed
 * action, not a live health check; "unknown" follows an interrupted tool.
 * SHA-256 is lowercase hex, set only after a complete read. published can be
 * true on a durability or cleanup error after atomic replacement. */
typedef struct {
  uint32_t address, size;
  uint32_t flash_address,
      flash_size; /**< Complete physical flash map, bytes. */
  bool complete;  /**< The requested range covers the complete flash. */
  char sha256[65];
  const char *target, *device_state; /**< Static strings, never caller-owned. */
  jb_process_result_t process;
  bool attempted, published;
  uint32_t timeout_ms, baud_rate; /**< Effective process timeout and ESP baud
                                     (zero on RP/STM). */
} jh_firmware_result_t;

/** Read a selected build's complete physical flash or a range within its map,
 * staging below the build directory, then atomically publish output.
 * Inputs/result/location/failure are required. Errors name missing/ambiguous
 * builds, invalid ranges, tool failures or I/O failures in failure (exit 2/3/4,
 * 130 on cancellation). Result is reset before validation. At most 64 MiB per
 * dump, 16 MiB per metadata file, 512 directories in automatic discovery.
 * Existing output survives a tool, length, cancellation or staging error.
 * Destination creation is checked before starting the reader. After a complete
 * read, publication/cancellation/cleanup errors retain the dump in the build
 * directory and name it in failure. POSIX staging files use mode 0600;
 * Windows staging grants access only to the current user (privileged OS access
 * remains possible).
 * Automatic timeout is at least 120 seconds; ESP also budgets 60 seconds plus
 * 20 serial bits per byte at baud_rate (UART framing and worst-case escaping).
 * Native USB uses this conservative UART budget; --timeout-ms can override it.
 */
hal_status_t jh_firmware_read(const jh_firmware_options_t *options,
                              jh_firmware_result_t *result,
                              jh_json_location_t *location,
                              jh_failure_t *failure);
#ifdef __cplusplus
}
#endif
#endif
