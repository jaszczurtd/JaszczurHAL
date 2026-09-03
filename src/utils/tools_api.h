#pragma once

/**
 * @file tools_api.h
 * @brief Utility API aggregator retained for source include compatibility.
 *
 * New code should include the relevant `hal/<domain>/` header directly.
 * Function declarations live only in their owning modules.
 */

#include "hal/analog/hal_adc_utils.h"
#include "hal/codecs/hal_image.h"
#include "hal/core/hal_config.h"
#include "hal/core/hal_math.h"
#include "hal/core/hal_text.h"
#include "hal/core/jh_endian.h"
#include "hal/display/hal_pixel.h"
#include "hal/gps/hal_gps_nmea_utils.h"
#include "hal/network/hal_network_utils.h"
#include "hal/serial/hal_serial.h"
#include "hal/system/hal_periodic_random.h"
#include "hal/temperature/hal_ntc.h"
#include "hal/time/hal_time.h"
