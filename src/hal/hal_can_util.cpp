#include "hal_config.h"
#ifndef HAL_DISABLE_CAN

#include "hal_can.h"
#include "hal_gpio.h"
#include "hal_system.h"
#include "hal_serial.h"

hal_can_t hal_can_create_with_retry(uint8_t cs_pin,
                                     uint8_t int_pin,
                                     void (*isr)(void),
                                     int max_retries,
                                     void (*retry_idle)(void)) {
    for (int attempt = 0; attempt <= max_retries; attempt++) {
        hal_can_t h = hal_can_create(cs_pin);
        if (h) {
            if (int_pin != HAL_CAN_NO_INT_PIN) {
                hal_gpio_set_mode(int_pin, HAL_GPIO_INPUT);
                if (isr) {
                    hal_gpio_attach_interrupt(int_pin, isr, HAL_GPIO_IRQ_FALLING);
                }
            }
            return h;
        }

        hal_derr_limited("can", "init failed (attempt %d/%d)",
                         attempt + 1, max_retries + 1);

        if (attempt < max_retries) {
            if (retry_idle) {
                retry_idle();
            }
            hal_delay_ms(SECOND);
        }
    }
    return NULL;
}

int hal_can_process_all(hal_can_t h, hal_can_frame_cb_t cb) {
    if (!h || !cb) return 0;

    int count = 0;
    uint32_t id;
    uint8_t len;
    uint8_t buf[HAL_CAN_MAX_DATA_LEN];

    while (hal_can_receive(h, &id, &len, buf)) {
        if (id != 0 && len > 0) {
            cb(id, len, buf);
            count++;
        }
    }
    return count;
}

uint8_t hal_can_encode_temp_i8(float temp_c) {
    int32_t value = (int32_t)temp_c;

    if (value < -128) {
        value = -128;
    }
    if (value > 127) {
        value = 127;
    }

    return (uint8_t)(int8_t)value;
}

#endif /* HAL_DISABLE_CAN */
