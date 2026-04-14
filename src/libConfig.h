#ifndef T_LIB_CONFIG
#define T_LIB_CONFIG

/**
 * @file libConfig.h
 * @brief Backward-compatible redirect to hal/hal_config.h.
 *
 * All configuration is now centralised in hal_config.h.
 * This file exists only so that existing #include "libConfig.h"
 * statements continue to work without modification.
 */
#include "hal/hal_config.h"

#endif
