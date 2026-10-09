/* The late header of the CAN-HAT assembly loads after the early one and
 * takes its pins and capabilities from it; no macro is defined twice. */
#include "jh_hardware.h"

#include "jh_board_config.h"

#if !defined(HAL_STM32G474_CLOCK_HSE_160MHZ) || HAL_BOARD_CAN_CHANNEL_COUNT != 3
#error "the assembly selects the HSE tree and wires three channels"
#endif
#if HAL_LED_BUILTIN != JH_HW_NODE_BOARD_STATUS_LED_PIN_OUT ||                  \
    HAL_BOARD_HAS_CYW43 != JH_HW_HAS_CYW43
#error "the late header must use the early values"
#endif
#if HAL_CYW43_PIN_WL_ON != JH_HW_NODE_WIFI_RADIO_PIN_WL_ON || !JH_HW_HAS_CYW43
#error "the CYW43 pins come from the selected provider"
#endif

#define CHANNEL_ROW(backend, instance, rx, tx, standby, high, limit)           \
  {instance, rx, tx, standby, high, limit},
static const struct {
  unsigned instance, rx, tx, standby, high;
  uint32_t limit;
} rows[] = {HAL_BOARD_CAN_CHANNELS(CHANNEL_ROW)};

int main(void) {
  int failures = 0;
  failures +=
      rows[0].instance != 1u || rows[0].rx != JH_HW_NODE_HAT_CAN1_PIN_RX;
  failures += rows[1].tx != JH_HW_NODE_HAT_CAN2_PIN_TX ||
              rows[1].standby != JH_HW_NODE_HAT_CAN2_PIN_STANDBY;
  failures += rows[2].instance != 3u ||
              rows[2].limit != JH_HW_NODE_HAT_CAN3_PROP_MAX_BITRATE_HZ;
  failures += !rows[0].high;
  return failures;
}
