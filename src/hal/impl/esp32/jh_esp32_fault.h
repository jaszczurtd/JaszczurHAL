#pragma once

#include "hal/system/hal_system.h"

void jh_esp32_fault_init(void);
bool jh_esp32_fault_available(void);
bool jh_esp32_fault_get(hal_fault_info_t *out);
void jh_esp32_fault_clear(void);
