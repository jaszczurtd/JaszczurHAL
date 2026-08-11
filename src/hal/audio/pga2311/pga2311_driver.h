#pragma once

#include "hal/audio/hal_pga2311.h"
#ifdef HAL_ENABLE_PGA2311

#include <stdbool.h>
#include <stdint.h>

bool hal_pga2311_driver_validate_config(const hal_pga2311_config_t *cfg);
void hal_pga2311_driver_init_pins(const hal_pga2311_config_t *cfg);
void hal_pga2311_driver_set_hw_mute(const hal_pga2311_config_t *cfg, bool mute);
hal_status_t hal_pga2311_driver_write_codes(const hal_pga2311_config_t *cfg,
                                            uint8_t left_code,
                                            uint8_t right_code);

#endif /* HAL_ENABLE_PGA2311 */
