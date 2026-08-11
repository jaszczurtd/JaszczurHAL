#pragma once

/**
 * @file hal_hd44780.h
 * @brief HD44780-compatible parallel character LCD driver.
 *
 * Enable with HAL_ENABLE_HD44780 and include this header from C++ application
 * code. The implementation lives in hal/display/hd44780 and uses
 * JaszczurHAL GPIO, system timing and synchronization primitives.
 */

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_HD44780
#include "hal/display/hd44780/hd44780.h"
#endif
