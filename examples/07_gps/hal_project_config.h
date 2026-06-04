#pragma once

/* Library provides the entry-point (setup/loop/main). App defines
 * app_start(), app_task0(), and optionally app_task1(). See hal/hal_app.h. */
#define HAL_PROVIDE_APP_ENTRY

#define HAL_ENABLE_GPS
#define HAL_ENABLE_SWSERIAL   /* GPS transport on this RP2040 sketch (PA5/PA4) */
