#pragma once

/* Library provides the entry-point (setup/loop/main). App defines
 * app_start(), app_task0(), and optionally app_task1(). See hal/hal_app.h. */
#define HAL_PROVIDE_APP_ENTRY

#define HAL_ENABLE_KV
#define HAL_ENABLE_I2C
