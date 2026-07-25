#pragma once

/**
 * @file hal_usb.h
 * @brief USB device lifecycle and CDC transport abstraction.
 *
 * A target backend owns the complete USB device lifecycle. Serial-console
 * clients use this API and must not call TinyUSB or carrier USB APIs directly.
 */

#include "hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default maximum time spent waiting for CDC transmit capacity. */
#ifndef HAL_USB_CDC_WRITE_TIMEOUT_MS
#define HAL_USB_CDC_WRITE_TIMEOUT_MS 1000u
#endif

/** Magic CDC baud rate used by RP boards to request BOOTSEL mode. */
#ifndef HAL_USB_BOOTLOADER_TOUCH_BAUD
#define HAL_USB_BOOTLOADER_TOUCH_BAUD 1200u
#endif

/** Optional observer invoked immediately before a bootloader reset. */
typedef void (*hal_usb_bootloader_reset_hook_t)(void *user);

/**
 * @brief Initialise the target USB device and its background pump.
 *
 * The call is idempotent. On RP native targets it initialises TinyUSB,
 * descriptors, CDC and the background task pump. Compatibility carriers only
 * attach to the USB stack already owned by their runtime.
 */
hal_status_t hal_usb_init(void);

/**
 * @brief Stop the HAL USB background pump and disconnect the device.
 */
hal_status_t hal_usb_deinit(void);

/**
 * @brief Run one foreground USB device-task iteration.
 *
 * Foreground operations already pump the device while holding the USB mutex;
 * this entry point is available to cooperative runtimes and diagnostics.
 */
hal_status_t hal_usb_task(void);

/**
 * @brief Query whether the CDC host has asserted a usable connection.
 */
hal_status_t hal_usb_cdc_is_connected(bool *out_connected);

/**
 * @brief Query the number of bytes waiting in the CDC receive FIFO.
 */
hal_status_t hal_usb_cdc_available(size_t *out_available);

/**
 * @brief Read up to @p capacity bytes from CDC without waiting.
 *
 * @return HAL_OK when at least one byte was read, HAL_EAGAIN when no data is
 *         available, or another status on failure.
 */
hal_status_t hal_usb_cdc_read(uint8_t *data, size_t capacity, size_t *out_read);

/**
 * @brief Write bytes to CDC with bounded backpressure handling.
 *
 * @param timeout_ms Maximum time without transmit progress. Zero performs one
 *        nonblocking attempt.
 * @return HAL_OK when every byte was accepted, HAL_EAGAIN when disconnected,
 *         HAL_ETIMEOUT after partial/no progress, or another status on failure.
 */
hal_status_t hal_usb_cdc_write(const uint8_t *data, size_t length,
                               uint32_t timeout_ms, size_t *out_written);

/**
 * @brief Flush queued CDC data while continuing to pump the USB device.
 */
hal_status_t hal_usb_cdc_flush(uint32_t timeout_ms);

/**
 * @brief Enter the platform USB bootloader.
 *
 * Successful hardware implementations do not return.
 */
hal_status_t hal_usb_reset_to_bootloader(void);

/**
 * @brief Register an optional bootloader-reset observer.
 *
 * This is primarily useful for application shutdown bookkeeping and host/mock
 * tests. The hook must not block.
 */
hal_status_t
hal_usb_set_bootloader_reset_hook(hal_usb_bootloader_reset_hook_t hook,
                                  void *user);

#ifdef __cplusplus
}
#endif
