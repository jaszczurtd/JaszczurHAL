#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  HAL_CYW43_PIN_INPUT = 0,
  HAL_CYW43_PIN_OUTPUT = 1,
} hal_cyw43_pin_mode_t;

#ifdef __cplusplus
extern "C" {
#endif

void hal_cyw43_pinMode(uint8_t pin, hal_cyw43_pin_mode_t mode);
void hal_cyw43_digitalWrite(uint8_t pin, bool high);
bool hal_cyw43_digitalRead(uint8_t pin);

#ifdef __cplusplus
}
#endif
