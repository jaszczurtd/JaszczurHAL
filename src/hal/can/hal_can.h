#pragma once

#include "hal/core/hal_config.h"
#include "hal/core/hal_status.h"

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

/**
 * @brief Bit 31 of an identifier passed to or returned by the classic
 *        hal_can_send()/hal_can_receive(): the frame uses a 29-bit ID.
 *
 * Bits 28..0 then carry the extended ID; without this bit the low 11 bits are
 * the standard ID. The frame API uses @ref HAL_CAN_FRAME_EXTENDED instead.
 */
#define HAL_CAN_ID_EXTENDED_FLAG 0x80000000u

/**
 * @brief Bit 30 of a classic-API identifier: a remote (RTR) frame without
 *        payload. The frame API uses @ref HAL_CAN_FRAME_RTR instead.
 */
#define HAL_CAN_ID_RTR_FLAG 0x40000000u

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
  /** Internal loopback: own frames come back, the bus is not driven. */
  HAL_CAN_MODE_LOOPBACK = 0x01u,
  /** Receive only; the controller sends no ACK and no error frames. */
  HAL_CAN_MODE_LISTEN_ONLY = 0x02u,
  /** CAN FD frames allowed (FD-capable channels only). */
  HAL_CAN_MODE_FD = 0x04u,
  /** One transmission attempt per frame, no automatic retransmission. */
  HAL_CAN_MODE_ONE_SHOT = 0x08u,
  /** Controller held off the bus in a low-activity state. */
  HAL_CAN_MODE_SLEEP = 0x10u,
  /** External loopback: own frames come back and also go out on the bus,
   *  where missing ACKs are ignored. Frames come back from inside the
   *  controller, not from the transceiver, so this mode shows the frames on
   *  the bus but does not test reception (STM32G474 FDCAN). */
  HAL_CAN_MODE_EXTERNAL_LOOPBACK = 0x20u,
  /** After bus-off the node stays off the bus until hal_can_recover();
   *  without this flag it starts the recovery on its own. */
  HAL_CAN_MODE_MANUAL_RECOVERY = 0x40u
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
  /** Crystal or clock input: 40 or 20 MHz drive the controller directly,
   *  4 MHz turns on the x10 PLL (40 MHz). */
  uint32_t oscillator_hz;
  uint32_t
      spi_clock_hz; /**< SPI bus clock; 0 selects a conservative default. */
  /** Arbitration sample point in 0.1 %; 0 selects 80.0 %. */
  uint16_t arbitration_sample_point_permille;
  /** Data-phase sample point in 0.1 %; 0 selects 75.0 %. */
  uint16_t data_sample_point_permille;
  bool enable_fd;   /**< Allow CAN FD frames on this channel. */
  bool one_shot_tx; /**< Use one-shot TX attempts. */
  /** Wake-up filter on RXCAN and the wake-up interrupt enable. */
  bool sleep_wakeup;
} hal_can_mcp251xfd_config_t;

/** @brief Transmitter delay compensation of the CAN FD data phase. */
enum {
  /** On above 1 Mbit/s with a data prescaler of 1 or 2; offset at the
   *  sample point. */
  HAL_CAN_TDC_AUTO = 0u,
  /** Never used. */
  HAL_CAN_TDC_OFF = 1u,
  /** Always on with the offset from the config. */
  HAL_CAN_TDC_MANUAL = 2u
};

/** @brief STM32G474 native FDCAN backend configuration. */
typedef struct {
  /** FDCAN instance 1..3; 0 selects FDCAN1. */
  uint8_t instance;
  /** RX pin (port * 16 + pin); 0 selects the instance default: PA11, PB12 or
   *  PA8. Allowed: FDCAN1 PA11/PB8/PD0, FDCAN2 PB12/PB5, FDCAN3 PA8/PB3. */
  uint8_t rx_pin;
  /** TX pin; 0 selects the instance default: PA12, PB13 or PB4. Allowed:
   *  FDCAN1 PA12/PB9/PD1, FDCAN2 PB13/PB6, FDCAN3 PA15/PB4. */
  uint8_t tx_pin;
  /** Drive the transceiver standby pin; false leaves standby_pin alone. */
  bool has_standby;
  /** GPIO of the transceiver standby input. */
  uint8_t standby_pin;
  /** Level that puts the transceiver in standby (MCP2562FD: high). */
  bool standby_high;
  uint32_t arbitration_bitrate_hz; /**< Nominal/arbitration bitrate. */
  uint32_t data_bitrate_hz;        /**< CAN FD data bitrate; 0 selects
                                      arbitration_bitrate_hz. */
  /** Arbitration sample point in 0.1 %; 0 selects 80.0 %. */
  uint16_t arbitration_sample_point_permille;
  /** Data-phase sample point in 0.1 %; 0 selects 75.0 %. */
  uint16_t data_sample_point_permille;
  /** HAL_CAN_TDC_AUTO, HAL_CAN_TDC_OFF or HAL_CAN_TDC_MANUAL. */
  uint8_t tdc_mode;
  /** TDC offset in kernel clock periods (1..127), HAL_CAN_TDC_MANUAL only. */
  uint8_t tdc_offset;
  /** Highest bitrate the transceiver supports; 0 for no limit. */
  uint32_t transceiver_max_bitrate_hz;
  bool enable_fd;   /**< Allow CAN FD frames on this channel. */
  bool one_shot_tx; /**< Disable automatic retransmission. */
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
 * @return Configuration of the default backend: MCP2515 on SPI bus 0, CS 0,
 *         500 kbit/s with an 8 MHz crystal and one-shot TX; MCP251XFD and
 *         FDCAN with 500 kbit/s arbitration and a 2 Mbit/s data phase.
 */
hal_can_config_t hal_can_default_config(void);

/**
 * @brief Configuration of a CAN channel the board profile declares.
 *
 * Fills the controller, instance, pins, transceiver standby and transceiver
 * bitrate limit of board channel @p channel (0-based, in profile order) and
 * the defaults of a board channel: 500 kbit/s arbitration, CAN FD with a data
 * phase of 2 Mbit/s (or the transceiver limit when lower), automatic
 * retransmission. The result can be adjusted before hal_can_create().
 *
 * @param channel Board channel index, below HAL_BOARD_CAN_CHANNEL_COUNT.
 * @param[out] out Configuration to fill; untouched on error.
 * @return HAL_OK, HAL_EINVAL for a NULL @p out, or HAL_ENOENT when the board
 *         has no such channel.
 */
hal_status_t hal_can_board_config(uint8_t channel, hal_can_config_t *out);

/**
 * @brief Create and initialise a CAN channel.
 * @param cfg Configuration; NULL uses hal_can_default_config().
 * @param[out] out Handle on success, NULL otherwise.
 * @return HAL_OK, HAL_EINVAL for NULL @p out, HAL_EUNSUPPORTED for a backend
 *         that is not built in, HAL_ENOMEM when every handle is in use
 *         (raise HAL_CAN_MAX_INSTANCES), or the backend's initialization
 *         error (MCP2515: HAL_EUNSUPPORTED for a bitrate or crystal it has no
 *         timing for, HAL_EIO when the controller did not answer or did not
 *         take its configuration).
 */
hal_status_t hal_can_create(const hal_can_config_t *cfg, hal_can_t *out);

/**
 * @brief Release all resources associated with the CAN handle.
 * @param h Handle obtained from hal_can_create(). Must not be used after this
 * call.
 */
void hal_can_destroy(hal_can_t h);

/**
 * @brief Send a classic CAN data frame and wait until it went out.
 *
 * A standard 11-bit frame unless @p id carries @ref HAL_CAN_ID_EXTENDED_FLAG
 * or @ref HAL_CAN_ID_RTR_FLAG. Longer payloads are cut to
 * HAL_CAN_MAX_DATA_LEN bytes. The call returns after the controller sent the
 * frame or gave up (one-shot attempt failed, timeout, bus-off), also on a
 * queued channel.
 *
 * @param h   CAN handle.
 * @param id  CAN message identifier, optionally with the flag bits above.
 * @param len Payload length in bytes.
 * @param data Payload bytes; may be NULL only when @p len is 0.
 * @return HAL_OK when the frame went out, HAL_EINVAL for an invalid handle or
 *         a NULL @p data with a payload, HAL_EBUSY for a stopped or sleeping
 *         channel or when every transmit buffer of the controller stayed
 *         taken, HAL_ETIMEOUT when the frame did not leave in time (the
 *         controller dropped it), or HAL_EIO for a failed one-shot attempt (no
 *         ACK, a bus error or lost arbitration).
 */
hal_status_t hal_can_send(hal_can_t h, uint32_t id, uint8_t len,
                          const uint8_t *data);

/**
 * @brief Send a CAN or CAN FD frame and wait until it went out.
 *
 * A CAN FD frame needs @ref HAL_CAN_MODE_FD in the channel's current mode;
 * MCP2515 never has it. The call waits for its own frame also on a queued
 * channel and produces no send event; hal_can_send_frame_ex() queues the
 * frame instead.
 *
 * @param h CAN handle.
 * @param frame Frame to transmit.
 * @return HAL_OK when the frame went out, HAL_EINVAL for an invalid handle or
 *         frame, HAL_EUNSUPPORTED for an FD frame while the channel's mode has
 *         no HAL_CAN_MODE_FD, HAL_EBUSY for a stopped or sleeping channel or a
 *         controller without a free transmit buffer, or the error of the
 *         failed transmission (HAL_EIO, HAL_ETIMEOUT, HAL_EBUS when the node
 *         went bus-off).
 */
hal_status_t hal_can_send_frame(hal_can_t h, const hal_can_frame_t *frame);

/**
 * @brief Read the next available classic CAN frame without waiting.
 *
 * Extended and remote frames come back with @ref HAL_CAN_ID_EXTENDED_FLAG and
 * @ref HAL_CAN_ID_RTR_FLAG set in @p id. A CAN FD frame does not fit this
 * call: it is taken from the receive queue, counted in
 * hal_can_status_t::rx_dropped_fd_on_classic_read, and the call fails.
 *
 * @param h    CAN handle.
 * @param[out] id   Received message identifier with the flag bits above.
 * @param[out] len  Received payload length.
 * @param[out] data Payload buffer of at least HAL_CAN_MAX_DATA_LEN bytes.
 * @return HAL_OK, HAL_EINVAL for an invalid handle or a NULL output,
 *         HAL_EAGAIN when no frame waits, HAL_EUNSUPPORTED for a CAN FD
 *         frame it consumed, or HAL_EIO for a malformed frame object read
 *         from an MCP251XFD (a corrupted SPI transfer; the object is
 *         consumed).
 */
hal_status_t hal_can_receive(hal_can_t h, uint32_t *id, uint8_t *len,
                             uint8_t *data);

/**
 * @brief Take the oldest received frame without waiting and without metadata;
 *        hal_can_receive_frame_ex() with no info and a timeout of 0.
 * @param h CAN handle.
 * @param[out] frame Destination frame.
 * @return HAL_OK, HAL_EINVAL for an invalid handle or NULL @p frame,
 *         HAL_EAGAIN when no frame waits, or HAL_EIO for a malformed frame
 *         object read from an MCP251XFD.
 */
hal_status_t hal_can_receive_frame(hal_can_t h, hal_can_frame_t *frame);

/**
 * @brief Start the CAN controller.
 *
 * A newly created channel is started already. After hal_can_stop() this
 * reapplies the stored mode and the node joins the bus again.
 *
 * @param h CAN handle.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, or the backend's error
 *         (HAL_ETIMEOUT when the controller did not reach the mode, HAL_EIO
 *         for an SPI controller that did not answer).
 */
hal_status_t hal_can_start(hal_can_t h);

/**
 * @brief Stop the CAN controller; it leaves the bus.
 *
 * Sending fails and the state reads @ref HAL_CAN_STATE_STOPPED until
 * hal_can_start(). Filters and the stored mode are kept. On a queued
 * channel frames not yet sent end with HAL_CAN_TX_STOPPED.
 *
 * @param h CAN handle.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, or the backend's error
 *         (HAL_ETIMEOUT, HAL_EIO) when the controller did not stop.
 */
hal_status_t hal_can_stop(hal_can_t h);

/**
 * @brief Set the controller mode.
 *
 * Only one operating mode among LOOPBACK, EXTERNAL_LOOPBACK, LISTEN_ONLY and
 * SLEEP may be selected at once; no such flag means NORMAL. The new mode
 * replaces the old one, including ONE_SHOT, FD and MANUAL_RECOVERY.
 * hal_can_get_caps() lists the flags a channel accepts (MCP2515 has no FD).
 * On a started channel the mode applies at once; on a queued channel frames
 * not yet sent end with HAL_CAN_TX_STOPPED, because they were queued for the
 * old mode. On a stopped channel it is stored for hal_can_start().
 *
 * @param h CAN handle.
 * @param mode Bitwise OR of HAL_CAN_MODE_* flags.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, HAL_EUNSUPPORTED for a
 *         flag the channel does not have or two operating modes at once, or
 *         the backend's error when the controller did not take the mode
 *         (HAL_ETIMEOUT, HAL_EIO).
 */
hal_status_t hal_can_set_mode(hal_can_t h, hal_can_mode_t mode);

/**
 * @brief Read the controller mode last set or derived from the config.
 * @param h CAN handle.
 * @param[out] mode Bitwise OR of HAL_CAN_MODE_* flags.
 * @return HAL_OK, or HAL_EINVAL for an invalid handle or a NULL @p mode.
 */
hal_status_t hal_can_get_mode(hal_can_t h, hal_can_mode_t *mode);

/**
 * @brief Read the controller state.
 * @param h CAN handle.
 * @param[out] state @ref HAL_CAN_STATE_STOPPED while stopped, otherwise the
 *             error state the controller reports.
 * @return HAL_OK, HAL_EINVAL for an invalid handle or a NULL @p state, or the
 *         backend's error when the controller could not be read.
 */
hal_status_t hal_can_get_state(hal_can_t h, hal_can_state_t *state);

/**
 * @brief Read the transmit and receive error counters of the controller.
 * @param h CAN handle.
 * @param[out] counters TEC and REC as the controller holds them.
 * @return HAL_OK, HAL_EINVAL for an invalid handle or a NULL @p counters, or
 *         the backend's error when the controller could not be read.
 */
hal_status_t hal_can_get_error_counters(hal_can_t h,
                                        hal_can_error_counters_t *counters);

/**
 * @brief Check whether a received frame waits to be read.
 * @param h CAN handle.
 * @return HAL_OK when a frame can be read now, HAL_EAGAIN when none waits, or
 *         HAL_EINVAL for an invalid handle.
 */
hal_status_t hal_can_available(hal_can_t h);

/**
 * @brief Configure receive filters for standard (11-bit) CAN IDs.
 *
 * After this call the controller will only accept frames whose standard ID
 * matches @p id0 or @p id1 (exact match on all 11 bits).  Frames with
 * non-matching IDs are silently rejected by the hardware, keeping the
 * receive buffers free for the desired traffic. Uses classic slots 0 and 1.
 *
 * @param h   CAN handle.
 * @param id0 First accepted standard CAN ID  (11-bit, e.g. 0x7E0).
 * @param id1 Second accepted standard CAN ID (11-bit, e.g. 0x7DF).
 * @return HAL_OK, HAL_EINVAL for an invalid handle, HAL_EUNSUPPORTED for a
 *         backend with fewer than two slots, or the backend's error when a
 *         slot could not be written (MCP2515: HAL_ETIMEOUT when the
 *         controller did not enter or leave its configuration mode).
 */
hal_status_t hal_can_set_std_filters(hal_can_t h, uint32_t id0, uint32_t id1);

/**
 * @brief Configure one static acceptance-filter slot.
 *
 * Until the first slot is set the channel receives every frame; afterwards
 * only frames matching an enabled slot. MCP2515 exposes six filter slots.
 * Slots 0-1 share mask group 0; slots 2-5 share mask group 1. Updating one
 * slot updates the shared group mask used by sibling slots in the same
 * MCP2515 group. When the new filter has no room (STM32G474 FDCAN, a slot
 * switching to the full list of the other ID kind), the old one stays.
 *
 * @param h CAN handle.
 * @param index Slot 0 .. HAL_CAN_MAX_FILTERS - 1.
 * @param filter ID, mask and flags; see hal_can_validate_filter().
 * @return HAL_OK, HAL_EINVAL for an invalid handle, slot or filter,
 *         HAL_ENOMEM when the controller has no room for the new filter, or
 *         the backend's error for a failed write (MCP2515: HAL_ETIMEOUT).
 */
hal_status_t hal_can_set_filter(hal_can_t h, uint8_t index,
                                const hal_can_filter_t *filter);

/** @brief Filter kinds of hal_can_filter_ex_t::type. */
enum {
  HAL_CAN_FILTER_MASK = 0u,  /**< id1 = ID, id2 = mask (1 bits compared). */
  HAL_CAN_FILTER_RANGE = 1u, /**< id1 .. id2, both included. */
  HAL_CAN_FILTER_DUAL = 2u   /**< Exactly id1 or id2. */
};

/** @brief What a matching filter does (hal_can_filter_ex_t::action). */
enum {
  HAL_CAN_FILTER_ACCEPT = 0u, /**< Receive the frame. */
  HAL_CAN_FILTER_REJECT = 1u  /**< Drop the frame. */
};

/** @brief First index hal_can_add_filter() returns; 0 .. HAL_CAN_MAX_FILTERS
 *         - 1 are the slots of hal_can_set_filter(). */
#define HAL_CAN_FILTER_FIRST_ADDED HAL_CAN_MAX_FILTERS

/** @brief Hardware acceptance filter beyond the classic id/mask slots. */
typedef struct {
  uint8_t type;   /**< HAL_CAN_FILTER_MASK, _RANGE or _DUAL. */
  uint8_t action; /**< HAL_CAN_FILTER_ACCEPT or _REJECT. */
  uint8_t flags;  /**< HAL_CAN_FILTER_EXTENDED for 29-bit IDs. */
  uint32_t id1;   /**< ID, first ID of a range, or first of two IDs. */
  uint32_t id2;   /**< Mask, last ID of a range, or second of two IDs. */
} hal_can_filter_ex_t;

/**
 * @brief Add a filter while the channel runs.
 *
 * Filters are checked in the order the controller holds them; a new filter
 * takes the lowest free position, and the first filter that matches decides.
 * Frames no filter matches follow hal_can_set_unmatched_policy().
 * @param h CAN handle.
 * @param filter Filter to add.
 * @param[out] index Index for hal_can_remove_filter() and
 *             hal_can_rx_info_t::filter_index; HAL_CAN_FILTER_FIRST_ADDED or
 *             above.
 * @return HAL_OK, HAL_EINVAL for an invalid handle or filter,
 *         HAL_EUNSUPPORTED on a backend without them, or HAL_ENOMEM when the
 *         controller has no free position for this ID kind.
 */
hal_status_t hal_can_add_filter(hal_can_t h, const hal_can_filter_ex_t *filter,
                                uint8_t *index);

/**
 * @brief Remove a filter added by hal_can_add_filter() or a classic slot
 *        programmed by hal_can_set_filter().
 * @param h CAN handle.
 * @param index Index hal_can_add_filter() returned, or a classic slot
 *        0 .. HAL_CAN_MAX_FILTERS - 1.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, HAL_EUNSUPPORTED on a
 *         backend without added filters (MCP2515), or HAL_ENOENT when
 *         @p index holds no filter.
 */
hal_status_t hal_can_remove_filter(hal_can_t h, uint8_t index);

/**
 * @brief What happens to frames no filter matches, and to remote frames.
 *
 * Without any filter a channel accepts everything. The first
 * hal_can_set_filter() switches standard and extended frames to rejected
 * unless this function was called before. Accepting unmatched frames keeps
 * the last filter position of that ID kind for itself.
 * @param h CAN handle.
 * @param accept_std Receive standard frames no filter matches.
 * @param accept_ext Receive extended frames no filter matches.
 * @param accept_rtr Receive remote frames at all; changing it needs a
 *        stopped channel on STM32G474 FDCAN.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, HAL_EUNSUPPORTED,
 *         HAL_ENOMEM when the last position is taken by a filter, or
 *         HAL_EBUSY for a remote-frame change on a running channel.
 */
hal_status_t hal_can_set_unmatched_policy(hal_can_t h, bool accept_std,
                                          bool accept_ext, bool accept_rtr);

/**
 * @brief Check a filter before it is added.
 * @param filter Filter to check; may be NULL.
 * @return HAL_OK for a known kind and action with IDs inside the 11- or
 *         29-bit range of the filter's ID kind and, for a range, id1 <= id2;
 *         HAL_EINVAL for NULL or anything else.
 */
hal_status_t hal_can_validate_filter_ex(const hal_can_filter_ex_t *filter);

/**
 * @brief Whether a frame meets a filter's condition, whatever its action.
 *
 * The ID kinds must agree (a standard filter never matches an extended
 * frame); then the mask, range or ID pair decides.
 * @param frame Frame to test.
 * @param filter Filter to test against.
 * @param[out] matches true when the frame meets the condition.
 * @return HAL_OK with @p matches set, or HAL_EINVAL for a NULL or invalid
 *         frame or filter or a NULL @p matches (then @p matches is untouched).
 */
hal_status_t hal_can_frame_matches_filter_ex(const hal_can_frame_t *frame,
                                             const hal_can_filter_ex_t *filter,
                                             bool *matches);

/** @brief Sentinel value indicating no interrupt pin should be configured. */
#define HAL_CAN_NO_INT_PIN 0xFF

/** @brief Callback invoked by hal_can_process_all() for each valid received
 * frame. */
typedef void (*hal_can_frame_cb_t)(uint32_t id, uint8_t len,
                                   const uint8_t *data);

/**
 * @brief Drain all pending frames, invoking a callback for each valid one.
 *
 * Reads frames with hal_can_receive() until none waits. Frames with id == 0
 * or len == 0 are skipped. A receive error ends the drain; the frames read
 * before it were delivered.
 *
 * @param h   CAN handle.
 * @param cb  Callback invoked per valid frame.
 * @param[out] delivered Optional count of frames handed to @p cb.
 * @return HAL_OK when the receive buffer is empty, HAL_EINVAL for an invalid
 *         handle or a NULL @p cb, or the hal_can_receive() error that ended
 *         the drain (HAL_EUNSUPPORTED for a CAN FD frame, HAL_EIO for a
 *         malformed MCP251XFD frame object).
 */
hal_status_t hal_can_process_all(hal_can_t h, hal_can_frame_cb_t cb,
                                 uint32_t *delivered);

/**
 * @brief Convert a CAN/CAN FD DLC value to payload byte count.
 * @param dlc Raw DLC 0..15 (9..15 are the CAN FD lengths 12..64).
 * @return Payload bytes, or 0 for invalid DLC values greater than 15.
 */
uint8_t hal_can_dlc_to_bytes(uint8_t dlc);

/**
 * @brief Convert payload byte count to the smallest CAN/CAN FD DLC.
 * @param bytes Payload length; values between CAN FD lengths round up.
 * @return DLC 0..15, or HAL_CAN_DLC_INVALID for payloads larger than 64
 *         bytes.
 */
uint8_t hal_can_bytes_to_dlc(uint8_t bytes);

/**
 * @brief Check that a frame can be put on a CAN bus.
 *
 * The ID fits its 11- or 29-bit format, the DLC is 0..15 and decodes to
 * @c len, classic frames carry at most 8 bytes, BRS and ESI need the FD flag
 * and remote frames are not FD frames.
 *
 * @param frame Frame to check; NULL is invalid.
 * @return HAL_OK when all the rules hold, HAL_EINVAL otherwise.
 */
hal_status_t hal_can_validate_frame(const hal_can_frame_t *frame);

/**
 * @brief Check that a filter's ID and mask fit the ID format it selects.
 * @param filter Filter to check; NULL is invalid.
 * @return HAL_OK for known flags and an ID and mask within 11 or 29 bits,
 *         HAL_EINVAL otherwise.
 */
hal_status_t hal_can_validate_filter(const hal_can_filter_t *filter);

/**
 * @brief Apply a filter in software, as the hardware would.
 * @param frame Frame to test.
 * @param filter Filter to test against.
 * @param[out] matches true when both use the same ID format and the masked
 *             IDs are equal.
 * @return HAL_OK with @p matches set, or HAL_EINVAL for a NULL or invalid
 *         frame or filter or a NULL @p matches (then @p matches is untouched).
 */
hal_status_t hal_can_frame_matches_filter(const hal_can_frame_t *frame,
                                          const hal_can_filter_t *filter,
                                          bool *matches);

/* ── Status-returning API: capabilities, queued sends, events, status ───── */

/** @brief Capability flags of hal_can_caps_t::features. */
enum {
  HAL_CAN_CAP_FD = 0x01u,              /**< CAN FD frames, bitrate switching. */
  HAL_CAN_CAP_TX_EVENTS = 0x02u,       /**< Queued sends report their outcome
                                            from the controller. */
  HAL_CAN_CAP_RX_QUEUE = 0x04u,        /**< Frames are queued on reception. */
  HAL_CAN_CAP_MANUAL_RECOVERY = 0x08u, /**< HAL_CAN_MODE_MANUAL_RECOVERY. */
  HAL_CAN_CAP_TIMESTAMP_HW = 0x10u     /**< Controller frame timestamps. */
};

/** @brief What a channel and its backend can do. */
typedef struct {
  hal_can_mode_t modes;   /**< HAL_CAN_MODE_* flags the channel accepts. */
  uint8_t std_filters;    /**< Standard-ID hardware filter elements. */
  uint8_t ext_filters;    /**< Extended-ID hardware filter elements. */
  uint8_t tx_slots;       /**< Frames the controller holds for sending. */
  uint8_t features;       /**< HAL_CAN_CAP_* flags. */
  uint32_t core_clock_hz; /**< Controller kernel clock; 0 when not known. */
  uint32_t max_nominal_bitrate_hz; /**< Highest arbitration bitrate. */
  uint32_t max_data_bitrate_hz;    /**< Highest data bitrate: the lower of the
                                        controller and transceiver limits. */
} hal_can_caps_t;

/** @brief Outcome reasons of hal_can_tx_event_t::reason. */
enum {
  HAL_CAN_TX_DONE = 0u,    /**< Sent and acknowledged. */
  HAL_CAN_TX_FAILED = 1u,  /**< The one-shot attempt failed: no ACK, a bus
                                error or lost arbitration. */
  HAL_CAN_TX_BUS_OFF = 2u, /**< Dropped when the node went bus-off. */
  HAL_CAN_TX_STOPPED = 3u  /**< Dropped by hal_can_stop() or a mode change. */
};

/** @brief Outcome of one queued send, delivered by hal_can_service(). */
typedef struct {
  uint32_t tag;          /**< Tag hal_can_send_frame_ex() returned. */
  hal_status_t result;   /**< HAL_OK when sent, otherwise an error. */
  uint8_t reason;        /**< HAL_CAN_TX_*. */
  uint64_t timestamp_us; /**< 0 until the backend has frame timestamps. */
} hal_can_tx_event_t;

/** @brief Filter index of a frame that passed without a matching filter. */
#define HAL_CAN_FILTER_NONE 0xFFu

/** @brief Metadata of a received frame. */
typedef struct {
  uint64_t timestamp_us; /**< 0 until the backend has frame timestamps. */
  uint8_t filter_index;  /**< Matching filter slot, or HAL_CAN_FILTER_NONE. */
} hal_can_rx_info_t;

/** @brief Counters and protocol state of a channel. */
typedef struct {
  hal_can_state_t state;   /**< Like hal_can_get_state(). */
  uint8_t tec;             /**< Transmit error counter. */
  uint8_t rec;             /**< Receive error counter. */
  uint8_t last_error;      /**< Last protocol error code (FDCAN LEC: 1 stuff,
                                2 form, 3 ACK, 4 bit 1, 5 bit 0, 6 CRC);
                                0 when none was seen. */
  uint8_t last_data_error; /**< Last data-phase error code (FDCAN DLEC). */
  uint8_t tdc_value;       /**< Measured transmitter delay (FDCAN TDCV). */
  uint32_t rx_frames;      /**< Frames received. */
  uint32_t tx_frames;      /**< Frames sent. */
  /** Overflows of the controller's receive FIFO. Each one lost at least one
   *  frame; the controller only latches that it happened, so the count is a
   *  lower bound of the frames lost there. */
  uint32_t rx_hw_lost;
  uint32_t rx_queue_overflow; /**< Frames dropped because the HAL receive
                                   queue was full. */
  uint32_t rx_dropped_fd_on_classic_read; /**< CAN FD frames consumed by the
                                               classic receive calls. */
  uint32_t tx_failed;      /**< Sends that ended without success. */
  uint32_t event_overflow; /**< Events dropped because the queue was full. */
  uint32_t bus_off_count;  /**< Times the node went bus-off. */
  /** Message RAM access failures of the controller (STM32G474 FDCAN MRAF):
   *  a received frame was dropped, or a transmission was aborted and the
   *  controller stopped sending (restricted operation mode), which the HAL
   *  ends at once. 0 on backends without message RAM. */
  uint32_t ram_access_failures;
} hal_can_status_t;

/**
 * @brief Receive callback, run by hal_can_service() in task context.
 * @param h Channel the frame arrived on.
 * @param frame Received frame, valid during the call only.
 * @param info Its timestamp and filter, valid during the call only.
 * @param user Pointer given to hal_can_set_callbacks().
 */
typedef void (*hal_can_rx_cb_t)(hal_can_t h, const hal_can_frame_t *frame,
                                const hal_can_rx_info_t *info, void *user);
/**
 * @brief Outcome callback of a send, run by hal_can_service().
 * @param h Channel of the send.
 * @param event Tag, result, reason and timestamp, valid during the call only.
 * @param user Pointer given to hal_can_set_callbacks().
 */
typedef void (*hal_can_tx_cb_t)(hal_can_t h, const hal_can_tx_event_t *event,
                                void *user);
/**
 * @brief State change callback, run by hal_can_service().
 * @param h Channel whose state changed.
 * @param state New state.
 * @param counters TEC and REC when the change was seen, valid during the call
 *        only.
 * @param user Pointer given to hal_can_set_callbacks().
 */
typedef void (*hal_can_state_cb_t)(hal_can_t h, hal_can_state_t state,
                                   const hal_can_error_counters_t *counters,
                                   void *user);

/** @brief Wait without a time limit (hal_can_send_frame_ex() and
 *         hal_can_receive_frame_ex() timeouts). */
#define HAL_CAN_WAIT_FOREVER 0xFFFFFFFFu

/**
 * @brief Capabilities of a channel.
 * @param h CAN handle.
 * @param[out] out Capabilities.
 * @return HAL_OK, or HAL_EINVAL for an invalid handle or NULL @p out.
 */
hal_status_t hal_can_get_caps(hal_can_t h, hal_can_caps_t *out);

/**
 * @brief Queue a frame for sending.
 *
 * On a channel with HAL_CAN_CAP_TX_EVENTS the frame is queued and its outcome
 * arrives later as a hal_can_tx_event_t through hal_can_service(). Other
 * channels send before returning; the event is queued as well and the return
 * value already carries the outcome.
 *
 * @param h CAN handle.
 * @param frame Frame to send.
 * @param timeout_ms How long to wait for room in a full queue; 0 does not
 *        wait, HAL_CAN_WAIT_FOREVER waits without a limit.
 * @param[out] tag Optional tag that identifies the frame in its event.
 * @return HAL_OK when queued (or sent), HAL_EINVAL for an invalid handle or
 *         frame, HAL_EUNSUPPORTED for an FD frame while the channel's mode
 *         has no HAL_CAN_MODE_FD,
 *         HAL_EBUSY for a stopped or sleeping channel or a full queue with
 *         @p timeout_ms 0, HAL_ETIMEOUT when the queue stayed full, or the
 *         send error of a channel without a queue.
 */
hal_status_t hal_can_send_frame_ex(hal_can_t h, const hal_can_frame_t *frame,
                                   uint32_t timeout_ms, uint32_t *tag);

/**
 * @brief Take the oldest received frame with its metadata.
 * @param h CAN handle.
 * @param[out] frame Received frame.
 * @param[out] info Optional metadata.
 * @param timeout_ms How long to wait for a frame; 0 does not wait.
 * @return HAL_OK, HAL_EINVAL for an invalid handle or NULL @p frame,
 *         HAL_EAGAIN when nothing waits and @p timeout_ms is 0,
 *         HAL_ETIMEOUT, or HAL_EIO for a malformed frame object read from
 *         an MCP251XFD.
 */
hal_status_t hal_can_receive_frame_ex(hal_can_t h, hal_can_frame_t *frame,
                                      hal_can_rx_info_t *info,
                                      uint32_t timeout_ms);

/**
 * @brief Set the callbacks hal_can_service() invokes; NULL disables one.
 *
 * With a receive callback set, hal_can_service() hands every received frame
 * to it; without one, frames wait for the receive calls. The new set
 * replaces the old one at once.
 * @param h CAN handle.
 * @param rx Called for each received frame; NULL leaves frames to the
 *        receive calls.
 * @param tx Called with the outcome of each send; NULL drops the outcomes.
 * @param state Called on each state change; NULL drops them.
 * @param user Passed unchanged to every callback; may be NULL.
 * @return HAL_OK, or HAL_EINVAL for an invalid handle.
 */
hal_status_t hal_can_set_callbacks(hal_can_t h, hal_can_rx_cb_t rx,
                                   hal_can_tx_cb_t tx, hal_can_state_cb_t state,
                                   void *user);

/**
 * @brief Deliver queued events to the callbacks, in task context.
 *
 * Send outcomes and state changes come first, then received frames when a
 * receive callback is set. Callbacks run without the handle lock held and may
 * call the other hal_can_* functions on the same handle.
 * @param h CAN handle.
 * @param max_events Most events to deliver; 0 or less delivers all waiting.
 * @return Events delivered, or HAL_EINVAL for an invalid handle.
 */
int hal_can_service(hal_can_t h, int max_events);

/**
 * @brief Function the receiving interrupt calls after queueing frames or
 *        events, e.g. to wake the task that calls hal_can_service().
 * @param h CAN handle.
 * @param notify Called from interrupt context; NULL removes it.
 * @param user Passed to @p notify.
 * @return HAL_OK, HAL_EINVAL for an invalid handle, or HAL_EUNSUPPORTED on a
 *         channel without an interrupt-fed queue.
 */
hal_status_t hal_can_set_isr_notify(hal_can_t h, void (*notify)(void *),
                                    void *user);

/**
 * @brief State, error counters, last errors and traffic counters.
 *
 * Counters count from hal_can_create() and wrap at 2^32. A stopped
 * channel reports @ref HAL_CAN_STATE_STOPPED. Backends without protocol
 * error codes or delay measurement leave those fields 0.
 * @param h CAN handle.
 * @param[out] out Status; untouched on error.
 * @return HAL_OK, HAL_EINVAL for an invalid handle or NULL @p out, or the
 *         backend's error when the controller could not be read.
 */
hal_status_t hal_can_get_status(hal_can_t h, hal_can_status_t *out);

/**
 * @brief Start the bus-off recovery of a channel in
 *        HAL_CAN_MODE_MANUAL_RECOVERY and wait for its end.
 *
 * The controller rejoins after 128 x 11 recessive bits.
 * @param h CAN handle.
 * @param timeout_ms How long to wait for the recovery to finish.
 * @return HAL_OK when the node is back (or was not bus-off), HAL_EINVAL for
 *         an invalid handle, HAL_EUNSUPPORTED without manual recovery mode,
 *         or HAL_ETIMEOUT.
 */
hal_status_t hal_can_recover(hal_can_t h, uint32_t timeout_ms);

/**
 * @brief Create a CAN channel with automatic retry and optional interrupt
 *        setup.
 *
 * Attempts the initialisation up to (@p max_retries + 1) times, with a ~1 s
 * delay between attempts. On success it optionally configures the interrupt
 * pin and attaches @p isr on the falling edge.
 *
 * @param cfg          CAN backend configuration. NULL uses
 *                     hal_can_default_config().
 * @param int_pin      Controller interrupt GPIO, or HAL_CAN_NO_INT_PIN to
 *                     skip.
 * @param isr          ISR for the falling edge on @p int_pin (may be NULL).
 * @param max_retries  Additional attempts after the first (0 = try once).
 * @param retry_idle   Called between retries (e.g. feed watchdog), or NULL.
 * @param[out] out     Handle on success, NULL otherwise.
 * @return HAL_OK, HAL_EINVAL for a NULL @p out, or the error of the last
 *         attempt (see hal_can_create()).
 */
hal_status_t hal_can_create_with_retry(const hal_can_config_t *cfg,
                                       uint8_t int_pin, void (*isr)(void),
                                       int max_retries,
                                       void (*retry_idle)(void),
                                       hal_can_t *out);

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
