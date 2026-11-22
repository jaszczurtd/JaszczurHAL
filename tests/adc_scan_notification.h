#pragma once
#include "hal/analog/hal_adc_scan.h"
#ifdef __cplusplus
extern "C" {
#endif
void adc_scan_notification_case(hal_adc_scan_config_t *config,
                                void (*feed)(void), bool from_isr);
#ifdef __cplusplus
}
#endif
