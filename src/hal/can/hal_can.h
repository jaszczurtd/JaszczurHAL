#pragma once

#include "hal/core/hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_CAN

/**
 * @file hal_can.h
 * @brief Hardware abstraction for CAN bus communication.
 */

#include <stdbool.h>
#include <stdint.h>

/** @brief Maximum classic CAN data payload length in bytes. */
#define HAL_CAN_MAX_DATA_LEN 8

/** @brief Maximum CAN FD data payload length in bytes. */
#define HAL_CAN_FD_MAX_DATA_LEN 64

/** @brief Invalid DLC sentinel returned by hal_can_bytes_to_dlc(). */
#define HAL_CAN_DLC_INVALID 0xFFu

/** @brief Standard 11-bit CAN identifier mask. */
#define HAL_CAN_STD_ID_MASK 0x7FFu

/** @brief Extended 29-bit CAN identifier mask. */
#define HAL_CAN_EXT_ID_MASK 0x1FFFFFFFu

/** @brief Minimum number of hardware acceptance filters exposed by CAN
 * backends. */
#define HAL_CAN_MAX_FILTERS 6u

/** @brief CAN frame flags used by hal_can_frame_t. */
enum {
  HAL_CAN_FRAME_EXTENDED = 0x01u, /**< 29-bit CAN identifier. */
  HAL_CAN_FRAME_RTR = 0x02u,      /**< Remote-transmission-request frame. */
  HAL_CAN_FRAME_FD = 0x04u,       /**< CAN FD frame. */
  HAL_CAN_FRAME_BRS = 0x08u,      /**< CAN FD bitrate-switch flag. */
  HAL_CAN_FRAME_ESI = 0x10u       /**< CAN FD error-state-indicator flag. */
};

/** @brief Backend-agnostic CAN/CAN FD frame container. */
typedef struct {
  uint32_t id;   /**< 11-bit standard ID or 29-bit extended ID. */
  uint8_t dlc;   /**< Raw CAN DLC value: 0..8 classic, 0..15 CAN FD. */
  uint8_t len;   /**< Payload byte count after DLC decoding. */
  uint8_t flags; /**< Bitwise OR of HAL_CAN_FRAME_* flags. */
  uint8_t data[HAL_CAN_FD_MAX_DATA_LEN]; /**< Payload bytes. */
} hal_can_frame_t;

/** @brief CAN filter flags used by hal_can_filter_t. */
enum {
  HAL_CAN_FILTER_EXTENDED = 0x01u /**< Match 29-bit CAN identifiers. */
};

/** @brief Backend-agnostic CAN acceptance filter. */
typedef struct {
  uint32_t id;   /**< Filter ID before mask application. */
  uint32_t mask; /**< 1 bits participate in matching, 0 bits are ignored. */
  uint8_t flags; /**< Bitwise OR of HAL_CAN_FILTER_* flags. */
} hal_can_filter_t;

/** @brief CAN controller operating mode flags. */
typedef uint32_t hal_can_mode_t;

enum {
  HAL_CAN_MODE_NORMAL = 0x00u,
  HAL_CAN_MODE_LOOPBACK = 0x01u,
  HAL_CAN_MODE_LISTEN_ONLY = 0x02u,
  HAL_CAN_MODE_FD = 0x04u,
  HAL_CAN_MODE_ONE_SHOT = 0x08u,
  HAL_CAN_MODE_SLEEP = 0x10u
};

/** @brief CAN controller state. */
typedef enum {
  HAL_CAN_STATE_ERROR_ACTIVE = 0,
  HAL_CAN_STATE_ERROR_WARNING,
  HAL_CAN_STATE_ERROR_PASSIVE,
  HAL_CAN_STATE_BUS_OFF,
  HAL_CAN_STATE_STOPPED
} hal_can_state_t;

/** @brief CAN controller error counters. */
typedef struct {
  uint8_t tx; /**< Transmit error counter. */
  uint8_t rx; /**< Receive error counter. */
} hal_can_error_counters_t;

/**
 * @brief Opaque handle for a CAN bus channel.
 *
 * One handle per physical CAN controller/backend instance.
 * Use hal_can_create() to obtain a handle; hal_can_destroy() to release it.
 */
typedef struct hal_can_impl_s hal_can_impl_t;
typedef hal_can_impl_t *hal_can_t;

/** @brief CAN backend selector. */
typedef enum {
  /** External Microchip MCP2515 controller over HAL SPI. */
  HAL_CAN_BACKEND_MCP2515 = 0,
  /** External Microchip MCP2517FD/MCP2518FD controller over HAL SPI. */
  HAL_CAN_BACKEND_MCP251XFD = 1,
  /** Native STM32G474 FDCAN1 controller. */
  HAL_CAN_BACKEND_STM32G474_FDCAN = 2
} hal_can_backend_t;

/** @brief MCP2515-specific backend configuration. */
typedef struct {
  uint8_t spi_bus;        /**< HAL SPI bus index. */
  uint8_t cs_pin;         /**< SPI chip-select pin for the MCP2515. */
  uint32_t bitrate_hz;    /**< CAN bitrate, e.g. 500000. */
  uint32_t oscillator_hz; /**< MCP2515 crystal frequency: 8000000, 16000000, or
                             20000000. */
  bool one_shot_tx;       /**< Enable one-shot TX mode after init. */
  bool sleep_wakeup;      /**< Enable wake-up interrupt support in MCP2515. */
} hal_can_mcp2515_config_t;

/** @brief MCP251XFD-specific backend configuration. */
typedef struct {
  uint8_t spi_bus; /**< HAL SPI bus index. */
  uint8_t cs_pin;  /**< SPI chip-select pin for the MCP251XFD. */
  uint32_t arbitration_bitrate_hz; /**< Nominal/arbitration bitrate. */
  uint32_t data_bitrate_hz;        /**< CAN FD data bitrate; 0 selects
                                      arbitration_bitrate_hz. */
  uint32_t oscillator_hz;          /**< MCP251XFD crystal frequency. */
  uint32_t
      spi_clock_hz;  /**< SPI bus clock; 0 selects a conservative default. */
  bool enable_fd;    /**< Allow CAN FD frames on this channel. */
  bool one_shot_tx;  /**< Use one-shot TX attempts. */
  bool sleep_wakeup; /**< Enable wake-up interrupt support where available. */
} hal_can_mcp251xfd_config_t;

/** @brief STM32G474 native FDCAN backend configuration. */
typedef struct {
  uint8_t rx_pin; /**< FDCAN RX pin; default example uses PA11. */
  uint8_t tx_pin; /**< FDCAN TX pin; default example uses PA12. */
  uint32_t arbitration_bitrate_hz; /**< Nominal/arbitration bitrate. */
  uint32_t data_bitrate_hz;        /**< CAN FD data bitrate; 0 selects
                                      arbitration_bitrate_hz. */
  bool enable_fd;                  /**< Allow CAN FD frames on this channel. */
  bool one_shot_tx;                /**< Disable automatic retransmission. */
} hal_can_stm32g474_fdcan_config_t;

/** @brief CAN channel configuration. */
typedef struct {
  hal_can_backend_t backend;
  union {
    hal_can_mcp2515_config_t mcp2515;
    hal_can_mcp251xfd_config_t mcp251xfd;
    hal_can_stm32g474_fdcan_config_t stm32g474_fdcan;
  };
} hal_can_config_t;

/**
 * @brief Return the default CAN config for the enabled backend set.
 *
 * If multiple backends are enabled, MCP2515 owns the compatibility default,
 * followed by MCP251XFD, then STM32G474 native FDCAN.
 */
hal_can_config_t hal_can_default_config(void);

/**
 * @brief Create and initialise a CAN channel from config.
 * @param cfg CAN backend configuration. NULL uses hal_can_default_config().
 * @return Handle on success, NULL on failure (chip not responding or pool
 * exhausted).
 */
hal_can_t hal_can_create(const hal_can_config_t *cfg);

/**
 * @brief Release all resources associated with the CAN handle.
 * @param h Handle obtained from hal_can_create(). Must not be used after this
 * call.
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
 * @brief Send a CAN or CAN FD frame.
 *
 * Backends that only support classic CAN, such as MCP2515, reject frames with
 * @ref HAL_CAN_FRAME_FD, @ref HAL_CAN_FRAME_BRS, or @ref HAL_CAN_FRAME_ESI.
 * Classic compatibility wrappers call this with a standard 11-bit data frame.
 *
 * @param h CAN handle.
 * @param frame Frame to transmit.
 * @return true on success, false if validation fails, the backend lacks the
 *         requested frame capability, or transmission fails.
 */
bool hal_can_send_frame(hal_can_t h, const hal_can_frame_t *frame);

/**
 * @brief Read the next available CAN frame.
 * @param h    CAN handle.
 * @param[out] id   Received message identifier.
 * @param[out] len  Received payload length.
 * @param[out] data Buffer for payload (must be at least HAL_CAN_MAX_DATA_LEN
 * bytes).
 * @return true if a frame was available, false if the RX buffer was empty
 *         or if any output pointer is NULL.
 */
bool hal_can_receive(hal_can_t h, uint32_t *id, uint8_t *len, uint8_t *data);

/**
 * @brief Read the next available CAN or CAN FD frame.
 * @param h CAN handle.
 * @param[out] frame Destination frame.
 * @return true if a frame was available, false otherwise.
 */
bool hal_can_receive_frame(hal_can_t h, hal_can_frame_t *frame);

/**
 * @brief Start the CAN controller.
 *
 * A newly created channel is started by default for compatibility with the
 * legacy send/receive API. Calling this after @ref hal_can_stop reapplies the
 * stored controller mode.
 */
bool hal_can_start(hal_can_t h);

/** @brief Stop the CAN controller and put the backend in a non-participating
 * state. */
bool hal_can_stop(hal_can_t h);

/**
 * @brief Set controller mode flags.
 *
 * MCP2515 supports NORMAL, LOOPBACK, LISTEN_ONLY, SLEEP, and ONE_SHOT. It does
 * not support HAL_CAN_MODE_FD. MCP251XFD and STM32G474 FDCAN support
 * HAL_CAN_MODE_FD when created with their enable_fd option. Only one operating
 * mode among LOOPBACK, LISTEN_ONLY, and SLEEP may be selected at once; no such
 * flag means NORMAL.
 */
bool hal_can_set_mode(hal_can_t h, hal_can_mode_t mode);

/** @brief Return the currently configured controller mode. */
bool hal_can_get_mode(hal_can_t h, hal_can_mode_t *mode);

/** @brief Read the current controller state. */
bool hal_can_get_state(hal_can_t h, hal_can_state_t *state);

/** @brief Read the current transmit/receive error counters. */
bool hal_can_get_error_counters(hal_can_t h,
                                hal_can_error_counters_t *counters);

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

/**
 * @brief Configure one static acceptance-filter slot.
 *
 * MCP2515 exposes six filter slots. Slots 0-1 share mask group 0; slots 2-5
 * share mask group 1. Updating one slot updates the shared group mask used by
 * sibling slots in the same MCP2515 group.
 */
bool hal_can_set_filter(hal_can_t h, uint8_t index,
                        const hal_can_filter_t *filter);

/** @brief Sentinel value indicating no interrupt pin should be configured. */
#define HAL_CAN_NO_INT_PIN 0xFF

/** @brief Callback invoked by hal_can_process_all() for each valid received
 * frame. */
typedef void (*hal_can_frame_cb_t)(uint32_t id, uint8_t len,
                                   const uint8_t *data);

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
 * @brief Convert a CAN/CAN FD DLC value to payload byte count.
 * @return 0 for invalid DLC values greater than 15.
 */
uint8_t hal_can_dlc_to_bytes(uint8_t dlc);

/**
 * @brief Convert payload byte count to the smallest CAN/CAN FD DLC.
 * @return HAL_CAN_DLC_INVALID for payloads larger than 64 bytes.
 */
uint8_t hal_can_bytes_to_dlc(uint8_t bytes);

/** @brief Return true when a frame has a valid ID/DLC/flag combination. */
bool hal_can_validate_frame(const hal_can_frame_t *frame);

/** @brief Return true when a filter has a valid ID/mask/flag combination. */
bool hal_can_validate_filter(const hal_can_filter_t *filter);

/** @brief Return true when @p frame matches @p filter. */
bool hal_can_frame_matches_filter(const hal_can_frame_t *frame,
                                  const hal_can_filter_t *filter);

/**
 * @brief Create a CAN channel with automatic retry and optional interrupt
 * setup.
 *
 * Attempts to initialise the MCP2515 up to (@p max_retries + 1) times,
 * with a ~1 s delay between attempts.  On success, optionally configures
 * the interrupt pin and attaches @p isr on falling edge.
 *
 * @param cfg          CAN backend configuration. NULL uses
 * hal_can_default_config().
 * @param int_pin      MCP2515 interrupt GPIO, or HAL_CAN_NO_INT_PIN to skip.
 * @param isr          ISR for falling edge on @p int_pin (may be NULL).
 * @param max_retries  Number of additional attempts after the first (0 = try
 * once).
 * @param retry_idle   Called between retries (e.g. feed watchdog), or NULL.
 * @return Valid handle on success, NULL if all attempts failed.
 */
hal_can_t hal_can_create_with_retry(const hal_can_config_t *cfg,
                                    uint8_t int_pin, void (*isr)(void),
                                    int max_retries, void (*retry_idle)(void));

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

#endif /* HAL_ENABLE_CAN */

#ifdef __cplusplus
}
#endif
