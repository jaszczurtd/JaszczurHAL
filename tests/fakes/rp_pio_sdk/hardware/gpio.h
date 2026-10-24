#pragma once

#include "pico/platform.h"

#include <stdbool.h>

#define NUM_BANK0_GPIOS 30u
#define GPIO_IN false
#define GPIO_OUT true

enum gpio_function { GPIO_FUNC_SIO = 5, GPIO_FUNC_PIO0 = 6 };

void gpio_pull_up(uint gpio);
void gpio_set_dir(uint gpio, bool out);
void gpio_set_function(uint gpio, enum gpio_function fn);
void gpio_put(uint gpio, bool value);
