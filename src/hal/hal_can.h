#pragma once

#include "hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifndef HAL_DISABLE_CAN

/**
 * @file hal_can.h
 * @brief Hardware abstraction for MCP2515-based CAN bus communication.
 */

#include <stdint.h>
#include <stdbool.h>

/** @brief Maximum CAN data payload length in bytes. */
#define HAL_CAN_MAX_DATA_LEN 8

/**
 * @brief Opaque handle for a CAN bus channel.
 *
 * One handle per physical MCP2515 chip (CS pin).
 * Use hal_can_create() to obtain a handle; hal_can_destroy() to release it.
 */
typedef struct hal_can_impl_s hal_can_impl_t;
typedef hal_can_impl_t *hal_can_t;

/**
 * @brief Create and initialise a CAN channel on the given CS pin at 500 kbps / 8 MHz.
 * @param cs_pin SPI chip-select pin for the MCP2515.
 * @return Handle on success, NULL on failure (chip not responding or pool exhausted).
 */
hal_can_t hal_can_create(uint8_t cs_pin);

/**
 * @brief Release all resources associated with the CAN handle.
 * @param h Handle obtained from hal_can_create(). Must not be used after this call.
 */
void hal_can_destroy(hal_can_t h);

/**
 * @brief Send a CAN frame.
 * @param h   CAN handle.
 * @param id  CAN message identifier.
 * @param len Payload length (must be <= HAL_CAN_MAX_DATA_LEN).
 * @param data Pointer to the payload bytes (required when @p len > 0).
 * @return true on success, false on failure.
 */
bool hal_can_send(hal_can_t h, uint32_t id, uint8_t len, const uint8_t *data);

/**
 * @brief Read the next available CAN frame.
 * @param h    CAN handle.
 * @param[out] id   Received message identifier.
 * @param[out] len  Received payload length.
 * @param[out] data Buffer for payload (must be at least HAL_CAN_MAX_DATA_LEN bytes).
 * @return true if a frame was available, false if the RX buffer was empty
 *         or if any output pointer is NULL.
 */
bool hal_can_receive(hal_can_t h, uint32_t *id, uint8_t *len, uint8_t *data);

/**
 * @brief Check if at least one frame is waiting in the RX buffer.
 * @param h CAN handle.
 * @return true if data is available.
 */
bool hal_can_available(hal_can_t h);

/**
 * @brief Configure receive filters for standard (11-bit) CAN IDs.
 *
 * After this call the controller will only accept frames whose standard ID
 * matches @p id0 or @p id1 (exact match on all 11 bits).  Frames with
 * non-matching IDs are silently rejected by the hardware, keeping the
 * receive buffers free for the desired traffic.
 *
 * @param h   CAN handle.
 * @param id0 First accepted standard CAN ID  (11-bit, e.g. 0x7E0).
 * @param id1 Second accepted standard CAN ID (11-bit, e.g. 0x7DF).
 * @return true on success, false if filter programming failed.
 */
bool hal_can_set_std_filters(hal_can_t h, uint32_t id0, uint32_t id1);

/** @brief Sentinel value indicating no interrupt pin should be configured. */
#define HAL_CAN_NO_INT_PIN 0xFF

/** @brief Callback invoked by hal_can_process_all() for each valid received frame. */
typedef void (*hal_can_frame_cb_t)(uint32_t id, uint8_t len, const uint8_t *data);

/**
 * @brief Drain all pending frames, invoking a callback for each valid one.
 *
 * Reads frames from the RX buffer until empty.  Frames with id == 0 or
 * len == 0 are silently skipped.
 *
 * @param h   CAN handle.
 * @param cb  Callback invoked per valid frame (must not be NULL).
 * @return    Number of frames delivered to @p cb.
 */
int hal_can_process_all(hal_can_t h, hal_can_frame_cb_t cb);

/**
 * @brief Create a CAN channel with automatic retry and optional interrupt setup.
 *
 * Attempts to initialise the MCP2515 up to (@p max_retries + 1) times,
 * with a ~1 s delay between attempts.  On success, optionally configures
 * the interrupt pin and attaches @p isr on falling edge.
 *
 * @param cs_pin       SPI chip-select pin for the MCP2515.
 * @param int_pin      MCP2515 interrupt GPIO, or HAL_CAN_NO_INT_PIN to skip.
 * @param isr          ISR for falling edge on @p int_pin (may be NULL).
 * @param max_retries  Number of additional attempts after the first (0 = try once).
 * @param retry_idle   Called between retries (e.g. feed watchdog), or NULL.
 * @return Valid handle on success, NULL if all attempts failed.
 */
hal_can_t hal_can_create_with_retry(uint8_t cs_pin,
                                     uint8_t int_pin,
                                     void (*isr)(void),
                                     int max_retries,
                                     void (*retry_idle)(void));

/**
 * @brief Encode temperature in °C as a signed int8 CAN payload byte.
 *
 * Input is truncated toward zero, saturated to the int8_t range, then
 * returned as the corresponding two's complement byte.
 *
 * @param temp_c Temperature in degrees Celsius.
 * @return Encoded byte representing int8_t range [-128, 127].
 */
uint8_t hal_can_encode_temp_i8(float temp_c);

#endif /* HAL_DISABLE_CAN */

#ifdef __cplusplus
}
#endif
