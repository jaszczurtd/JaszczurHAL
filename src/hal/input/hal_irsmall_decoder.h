#pragma once

#include "hal/core/hal_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef HAL_ENABLE_IRSMALL_DECODER

/**
 * @file hal_irsmall_decoder.h
 * @brief IRsmallDecoder-compatible infrared receiver decoder over HAL GPIO.
 *
 * The decoder uses GPIO interrupts and hal_micros() timing. Public calls are
 * serialized with an instance mutex created through the HAL create-once helper,
 * while ISR-shared state uses short critical sections where non-atomic values
 * need to be read or reset from task context. Do not call deinit concurrently
 * with other operations on the same instance.
 */

#include "hal/core/hal_status.h"
#include "hal/gpio/hal_gpio.h"
#include "hal/system/hal_sync.h"

#include <stdbool.h>
#include <stdint.h>

#define HAL_IRSMALL_DECODER_MAX_INSTANCES 4u

typedef enum {
  HAL_IRSMALL_PROTOCOL_NEC = 0,
  HAL_IRSMALL_PROTOCOL_NECX,
  HAL_IRSMALL_PROTOCOL_RC5,
  HAL_IRSMALL_PROTOCOL_SIRC12,
  HAL_IRSMALL_PROTOCOL_SIRC15,
  HAL_IRSMALL_PROTOCOL_SIRC20,
  HAL_IRSMALL_PROTOCOL_SIRC,
  HAL_IRSMALL_PROTOCOL_SAMSUNG,
  HAL_IRSMALL_PROTOCOL_SAMSUNG32,
} hal_irsmall_protocol_t;

typedef struct {
  hal_irsmall_protocol_t protocol;
  uint8_t input_pin;
  bool timeout_enabled;
  hal_irq_priority_t irq_priority;
} hal_irsmall_decoder_config_t;

typedef struct {
  hal_irsmall_protocol_t protocol;
  uint16_t addr;
  uint8_t cmd;
  uint8_t ext;
  bool key_held;
  uint8_t bits;
} hal_irsmall_decoder_data_t;

typedef struct {
  uint32_t signal;
  uint32_t first_code;
  uint32_t last_bit_time;
  uint16_t addr16;
  uint8_t bit_count;
  uint8_t repeat_count;
  uint8_t byte_index;
  uint8_t frame_count;
  uint8_t first_bit_count;
  uint8_t cmd;
  uint8_t bytes[4];
  bool possibly_held;
  bool prev_toggle;
} hal_irsmall_decoder_fsm_t;

typedef struct {
  hal_irsmall_decoder_config_t cfg;
  volatile hal_irsmall_decoder_data_t data;
  volatile hal_irsmall_decoder_fsm_t fsm;
  hal_mutex_t mutex;
  volatile bool initialized;
  volatile bool enabled;
  volatile bool data_available;
  volatile uint8_t state;
  volatile uint32_t previous_time;
  uint8_t slot_index;
} hal_irsmall_decoder_t;

hal_irsmall_decoder_config_t
hal_irsmall_decoder_default_config(uint8_t input_pin,
                                   hal_irsmall_protocol_t protocol);

bool hal_irsmall_decoder_init(hal_irsmall_decoder_t *dev,
                              const hal_irsmall_decoder_config_t *cfg);

hal_status_t
hal_irsmall_decoder_init_ex(hal_irsmall_decoder_t *dev,
                            const hal_irsmall_decoder_config_t *cfg);

void hal_irsmall_decoder_deinit(hal_irsmall_decoder_t *dev);

hal_status_t hal_irsmall_decoder_enable(hal_irsmall_decoder_t *dev);

hal_status_t hal_irsmall_decoder_disable(hal_irsmall_decoder_t *dev);

hal_status_t hal_irsmall_decoder_reset(hal_irsmall_decoder_t *dev);

hal_status_t
hal_irsmall_decoder_data_available_ex(hal_irsmall_decoder_t *dev,
                                      hal_irsmall_decoder_data_t *out,
                                      bool *out_available);

bool hal_irsmall_decoder_data_available(hal_irsmall_decoder_t *dev,
                                        hal_irsmall_decoder_data_t *out);

hal_status_t hal_irsmall_decoder_has_data_ex(hal_irsmall_decoder_t *dev,
                                             bool *out_has_data);

bool hal_irsmall_decoder_has_data(hal_irsmall_decoder_t *dev);

uint32_t hal_irsmall_decoder_timeout_us(hal_irsmall_protocol_t protocol);

hal_gpio_irq_mode_t
hal_irsmall_decoder_irq_mode(hal_irsmall_protocol_t protocol);

#endif /* HAL_ENABLE_IRSMALL_DECODER */
#ifdef __cplusplus
}
#endif
