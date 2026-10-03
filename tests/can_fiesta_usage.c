/* CAN API used exactly the way the Fiesta modules use it (ECU CAN0 and OBD,
 * Clocks, OilAndSpeed, Fiesta_clock). Compiled as C so the anonymous config
 * union, the positional filter initializer and the status results are checked
 * by a C compiler, like in the ECU. */
#include "can_fiesta_usage.h"

#include <stddef.h>

/* Fiesta reacts to the reason of a failure; a bool result would hide it
 * again, and a status where Fiesta expects a bool would read every error as
 * "true". */
#define FIESTA_RETURNS(expr, type)                                             \
  _Static_assert(_Generic((expr), type: 1, default: 0),                        \
                 #expr " must return " #type)

FIESTA_RETURNS(hal_can_send(NULL, 0u, 0u, NULL), hal_status_t);
FIESTA_RETURNS(hal_can_receive(NULL, NULL, NULL, NULL), hal_status_t);
FIESTA_RETURNS(hal_can_set_std_filters(NULL, 0u, 0u), hal_status_t);
FIESTA_RETURNS(hal_can_set_filter(NULL, 0u, NULL), hal_status_t);
FIESTA_RETURNS(hal_can_get_state(NULL, NULL), hal_status_t);
FIESTA_RETURNS(hal_can_available(NULL), hal_status_t);
FIESTA_RETURNS(hal_can_get_mode(NULL, NULL), hal_status_t);
FIESTA_RETURNS(hal_can_process_all(NULL, NULL, NULL), hal_status_t);
FIESTA_RETURNS(hal_can_encode_temp_i8(0.0f), uint8_t);
FIESTA_RETURNS(hal_can_create_with_retry(NULL, 0u, NULL, 0, NULL, NULL),
               hal_status_t);
FIESTA_RETURNS(hal_can_default_config(), hal_can_config_t);

_Static_assert(HAL_CAN_MAX_DATA_LEN == 8, "Fiesta buffers are 8 bytes");
_Static_assert(HAL_CAN_MAX_FILTERS == 6u,
               "Clocks programs every slot below HAL_CAN_MAX_FILTERS");
_Static_assert(HAL_CAN_STATE_ERROR_ACTIVE == 0, "state values are stable");
_Static_assert(HAL_CAN_MODE_NORMAL == 0x00u && HAL_CAN_MODE_ONE_SHOT == 0x08u,
               "mode flag values are stable");
_Static_assert(offsetof(hal_can_filter_t, id) <
                       offsetof(hal_can_filter_t, mask) &&
                   offsetof(hal_can_filter_t, mask) <
                       offsetof(hal_can_filter_t, flags),
               "Clocks initializes hal_can_filter_t positionally");

hal_status_t fiesta_ecu_can0_init(void (*isr)(void), int retries,
                                  void (*idle)(void), hal_can_t *out) {
  hal_can_config_t cfg = hal_can_default_config();
  cfg.mcp2515.cs_pin = FIESTA_CAN0_CS;
  cfg.mcp2515.one_shot_tx = true;
  return hal_can_create_with_retry(&cfg, FIESTA_CAN0_INT, isr, retries, idle,
                                   out);
}

hal_status_t fiesta_obd_init(void (*idle)(void), hal_can_t *out) {
  hal_can_config_t cfg = hal_can_default_config();
  cfg.mcp2515.cs_pin = FIESTA_CAN1_CS;
  hal_status_t st =
      hal_can_create_with_retry(&cfg, FIESTA_CAN1_INT, NULL, 1, idle, out);
  if (st == HAL_OK) {
    st = hal_can_set_std_filters(*out, 0x7E0u, 0x7DFu);
    if (st != HAL_OK) {
      hal_can_destroy(*out);
      *out = NULL;
    }
  }
  return st;
}

hal_status_t fiesta_clocks_filters(hal_can_t h) {
  const hal_can_filter_t filter = {0x120u, 0x7E0u, 0u};
  hal_status_t st = HAL_OK;
  for (uint8_t index = 0u; index < HAL_CAN_MAX_FILTERS && st == HAL_OK;
       index++) {
    st = hal_can_set_filter(h, index, &filter);
  }
  return st;
}

hal_status_t fiesta_send8(hal_can_t h, uint32_t id, const uint8_t *buf) {
  return hal_can_send(h, id, HAL_CAN_MAX_DATA_LEN, buf);
}

bool fiesta_bus_off(hal_can_t h) {
  hal_can_state_t state = HAL_CAN_STATE_ERROR_ACTIVE;
  return hal_can_get_state(h, &state) == HAL_OK &&
         state == HAL_CAN_STATE_BUS_OFF;
}

hal_status_t fiesta_obd_receive(hal_can_t h, uint32_t *id, uint8_t *len,
                                uint8_t buf[HAL_CAN_MAX_DATA_LEN]) {
  return hal_can_receive(h, id, len, buf);
}
