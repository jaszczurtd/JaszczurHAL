/**
 * @file link_probe.c
 * @brief Build-only ESP-IDF application entry probe for the library archive.
 */

#include "hal/core/hal_app.h"
#include "hal/system/hal_system.h"

void app_start(void) {}

void app_task0(void) { hal_idle(); }

void app_task1(void) { hal_idle(); }
