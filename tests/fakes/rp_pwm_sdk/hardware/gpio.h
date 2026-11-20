#pragma once

#include <stdint.h>

typedef unsigned int uint;

enum gpio_function { GPIO_FUNC_PWM = 4 };

void gpio_set_function(uint gpio, enum gpio_function function);
