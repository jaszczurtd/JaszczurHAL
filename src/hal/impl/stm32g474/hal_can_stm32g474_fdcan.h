#pragma once

#include "../../hal_can.h"

typedef struct {
  bool initialized;
  bool fd_capable;
  bool one_shot;
  uint8_t std_filter_count;
  uint8_t ext_filter_count;
} hal_can_stm32g474_fdcan_t;

bool hal_can_stm32g474_fdcan_init(hal_can_stm32g474_fdcan_t *fdcan,
                                  const hal_can_stm32g474_fdcan_config_t *cfg);
void hal_can_stm32g474_fdcan_deinit(hal_can_stm32g474_fdcan_t *fdcan);
bool hal_can_stm32g474_fdcan_send(hal_can_stm32g474_fdcan_t *fdcan, uint32_t id,
                                  uint8_t len, const uint8_t *data);
bool hal_can_stm32g474_fdcan_receive(hal_can_stm32g474_fdcan_t *fdcan,
                                     uint32_t *id, uint8_t *len, uint8_t *data);
bool hal_can_stm32g474_fdcan_send_frame(hal_can_stm32g474_fdcan_t *fdcan,
                                        const hal_can_frame_t *frame);
bool hal_can_stm32g474_fdcan_receive_frame(hal_can_stm32g474_fdcan_t *fdcan,
                                           hal_can_frame_t *frame);
bool hal_can_stm32g474_fdcan_available(hal_can_stm32g474_fdcan_t *fdcan);
bool hal_can_stm32g474_fdcan_set_filter(hal_can_stm32g474_fdcan_t *fdcan,
                                        uint8_t index,
                                        const hal_can_filter_t *filter);
bool hal_can_stm32g474_fdcan_set_std_filters(hal_can_stm32g474_fdcan_t *fdcan,
                                             uint32_t id0, uint32_t id1);
bool hal_can_stm32g474_fdcan_start(hal_can_stm32g474_fdcan_t *fdcan,
                                   hal_can_mode_t mode);
bool hal_can_stm32g474_fdcan_stop(hal_can_stm32g474_fdcan_t *fdcan);
bool hal_can_stm32g474_fdcan_set_mode(hal_can_stm32g474_fdcan_t *fdcan,
                                      hal_can_mode_t mode);
bool hal_can_stm32g474_fdcan_get_state(hal_can_stm32g474_fdcan_t *fdcan,
                                       bool started, hal_can_state_t *state);
bool hal_can_stm32g474_fdcan_get_error_counters(
    hal_can_stm32g474_fdcan_t *fdcan, hal_can_error_counters_t *counters);
