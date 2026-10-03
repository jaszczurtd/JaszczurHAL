#pragma once

/**
 * @file hal_can_stm32g474_fdcan.h
 * @brief Native STM32G474 FDCAN provider of the CAN facade.
 *
 * One context drives one of the three FDCAN instances. The message RAM of an
 * instance has a fixed layout (RM0440 §44.3.6); filters are written into it
 * while the controller runs, so changing them never takes the node off the
 * bus.
 */

#include "hal/can/hal_can.h"
#include "hal/can/jh_can_provider.h"

/** @brief Number of TX FIFO elements of one instance. */
#define HAL_CAN_STM32G474_FDCAN_TX_SLOTS 3u
/** @brief Standard and extended filter elements of one instance. */
#define HAL_CAN_STM32G474_FDCAN_STD_FILTERS 28u
#define HAL_CAN_STM32G474_FDCAN_EXT_FILTERS 8u

/** @brief State of one FDCAN instance behind a CAN handle. */
typedef struct {
  uint8_t index; /**< 0..2 for FDCAN1..3. */
  bool initialized;
  bool fd_capable; /**< Created with enable_fd. */
  bool has_standby;
  uint8_t standby_pin;
  bool standby_high;
  /** Unmatched-frame policy chosen (explicitly or by the first classic
   *  filter); later classic filters leave it alone. */
  bool policy_set;
  /** Filter index owning each element: 0xFF free, 0xFE accept-all. */
  uint8_t std_owner[HAL_CAN_STM32G474_FDCAN_STD_FILTERS];
  uint8_t ext_owner[HAL_CAN_STM32G474_FDCAN_EXT_FILTERS];
  uint32_t tx_timeout_us; /**< Bound of a blocking send. */
  uint32_t rx_hw_lost;    /**< Lost-frame events (RFnL) before attach. */
  /** Message RAM access failures (MRAF) seen without interrupts. */
  uint32_t ram_access_failures;
  hal_can_mode_t mode; /**< Mode last applied. */
  /** Facade queues once the interrupts run; NULL before attach. */
  jh_can_queues_t *queues;
  /** What each TX FIFO element carries (free, queued frame, blocking send)
   *  and the tag of a queued one; changed with interrupts masked. */
  volatile uint8_t slot_kind[HAL_CAN_STM32G474_FDCAN_TX_SLOTS];
  uint32_t slot_tag[HAL_CAN_STM32G474_FDCAN_TX_SLOTS];
  /** SOF time of a sent queued frame from the TX event FIFO
   *  (HAL_CAN_STM32G474_TIMESTAMP_TIM3), valid until its outcome is posted. */
  uint64_t slot_sof_us[HAL_CAN_STM32G474_FDCAN_TX_SLOTS];
  bool slot_sof_valid[HAL_CAN_STM32G474_FDCAN_TX_SLOTS];
  hal_can_state_t reported_state; /**< Last state posted as an event. */
  uint8_t last_error;      /**< LEC latched from PSR reads (reads reset it). */
  uint8_t last_data_error; /**< DLEC latched the same way. */
} hal_can_stm32g474_fdcan_t;

/** @brief Native FDCAN provider of the CAN facade. */
extern const jh_can_provider_t jh_can_stm32g474_fdcan_provider;
