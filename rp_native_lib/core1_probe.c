/**
 * @file core1_probe.c
 * @brief Build-only probe for the native app entry and core-1 policy.
 */

#include "hal/core/hal_app.h"
#include "hal/system/hal_system.h"

void app_start(void) {}

void app_task0(void) { hal_idle(); }

void app_task1(void) { hal_idle(); }
