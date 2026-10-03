#pragma once

/**
 * @file hal_can_mock_model.h
 * @brief Host model of a CAN controller behind the CAN facade.
 *
 * The facade stores one context per handle; the hal_mock_can_* helpers in
 * hal_mock.h reach it through the handle.
 */

#include "hal/core/hal_config.h"
#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_MOCK && defined(HAL_ENABLE_CAN)

#include "hal/can/jh_can_provider.h"

/** @brief Filter positions of the model, like the FDCAN lists. */
#define JH_CAN_MOCK_STD_FILTERS 28u
#define JH_CAN_MOCK_EXT_FILTERS 8u
/** @brief Owner of a free filter position. */
#define JH_CAN_MOCK_FREE 0xFFu

/** @brief One filter position: the filter and the index that owns it. */
typedef struct {
  uint8_t owner; /**< API filter index, JH_CAN_MOCK_FREE when unused. */
  hal_can_filter_ex_t filter;
} jh_can_mock_element_t;

/** @brief Controller model state of one mock CAN channel. */
typedef struct {
  hal_can_frame_t rx[MOCK_CAN_BUF_SIZE];
  int rx_head, rx_tail, rx_count;
  hal_can_frame_t tx[MOCK_CAN_BUF_SIZE];
  int tx_head, tx_tail, tx_count;
  hal_can_state_t state;
  hal_can_error_counters_t counters;
  /* Filters checked in position order; the first match decides. */
  jh_can_mock_element_t std_filters[JH_CAN_MOCK_STD_FILTERS];
  jh_can_mock_element_t ext_filters[JH_CAN_MOCK_EXT_FILTERS];
  bool accept_std; /**< Unmatched standard frames pass. */
  bool accept_ext; /**< Unmatched extended frames pass. */
  bool accept_rtr; /**< Remote frames pass at all. */
  bool policy_set; /**< Policy chosen; the first classic filter keeps it. */
  /* Queued, interrupt-style operation on the facade queues, like the native
   * FDCAN backend (hal_mock_can_set_queued()). */
  bool queued;
  jh_can_queues_t *queues; /**< Set by attach. */
  hal_can_mode_t mode;
  unsigned fail_sends;          /**< Queued sends still to end as failed. */
  uint32_t ram_access_failures; /**< Reported without the facade queues. */
} jh_can_mock_ctx_t;

/** @brief Mock provider serving every backend the build enables. */
extern const jh_can_provider_t jh_can_mock_provider;

#endif
