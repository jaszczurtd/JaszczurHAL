#pragma once

/* C entry points that call the CAN API the way Fiesta does. */

#include "hal/can/hal_can.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FIESTA_CAN0_CS 17u
#define FIESTA_CAN0_INT 15u
#define FIESTA_CAN1_CS 6u
#define FIESTA_CAN1_INT 14u

hal_status_t fiesta_ecu_can0_init(void (*isr)(void), int retries,
                                  void (*idle)(void), hal_can_t *out);
hal_status_t fiesta_obd_init(void (*idle)(void), hal_can_t *out);
hal_status_t fiesta_clocks_filters(hal_can_t h);
hal_status_t fiesta_send8(hal_can_t h, uint32_t id, const uint8_t *buf);
bool fiesta_bus_off(hal_can_t h);
hal_status_t fiesta_obd_receive(hal_can_t h, uint32_t *id, uint8_t *len,
                                uint8_t buf[HAL_CAN_MAX_DATA_LEN]);

#ifdef __cplusplus
}
#endif
