#pragma once
#define GPIO_FUNC_I2C 3U
inline void gpio_set_function(unsigned int, unsigned int) {}
inline void gpio_pull_up(unsigned int) {}
#define GPIO_IN false
#define GPIO_OUT true
inline void gpio_init(unsigned int) {}
inline void gpio_set_dir(unsigned int, bool) {}
inline void gpio_put(unsigned int, bool) {}
inline bool gpio_get(unsigned int) { return true; }
