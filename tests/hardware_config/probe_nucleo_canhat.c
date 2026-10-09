/* CAN-HAT channels as HAL configurations. */
#include "jh_hardware.h"

#include "hal/can/hal_can.h"

static const hal_can_config_t channels[] = {JH_HW_NODE_HAT_CAN1_CONFIG_INIT,
                                            JH_HW_NODE_HAT_CAN2_CONFIG_INIT,
                                            JH_HW_NODE_HAT_CAN3_CONFIG_INIT};
static const unsigned rx[] = {JH_HW_NODE_HAT_CAN1_PIN_RX,
                              JH_HW_NODE_HAT_CAN2_PIN_RX,
                              JH_HW_NODE_HAT_CAN3_PIN_RX};
static const unsigned standby[] = {JH_HW_NODE_HAT_CAN1_PIN_STANDBY,
                                   JH_HW_NODE_HAT_CAN2_PIN_STANDBY,
                                   JH_HW_NODE_HAT_CAN3_PIN_STANDBY};

int main(void) {
  int failures = 0;
  for (unsigned i = 0; i < 3u; ++i) {
    const hal_can_stm32g474_fdcan_config_t *fd = &channels[i].stm32g474_fdcan;
    failures += channels[i].backend != HAL_CAN_BACKEND_STM32G474_FDCAN;
    failures += fd->instance != i + 1u || fd->rx_pin != rx[i];
    failures +=
        !fd->has_standby || fd->standby_pin != standby[i] || !fd->standby_high;
    failures += fd->transceiver_max_bitrate_hz !=
                JH_HW_NODE_HAT_CAN1_PROP_MAX_BITRATE_HZ;
    failures +=
        fd->data_bitrate_hz != 2000000u || fd->one_shot_tx || !fd->enable_fd;
  }
  failures += JH_HW_NODE_HAT_LED_CAN2_TX_PIN_OUT_OWNER != 0;
  failures += JH_HW_NODE_HAT_RELAY1_CONFIG_AVAILABLE != 0;
  return failures;
}
