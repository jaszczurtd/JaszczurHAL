#pragma once

typedef int gpio_num_t;

#define GPIO_IS_VALID_GPIO(gpio_num) ((gpio_num) >= 0 && (gpio_num) < 49)
/* ESP32-S3: GPIO 0-48 are valid, and every valid GPIO can drive an output. */
#define GPIO_IS_VALID_OUTPUT_GPIO(gpio_num) GPIO_IS_VALID_GPIO(gpio_num)
