#pragma once

/**
 * @file JaszczurHAL.h
 * @brief Top-level include for the JaszczurHAL library.
 *
 * arduino-cli discovers a library by matching the sketch's #include directives
 * against the root-level header files in the library's src/ directory.
 * This file serves as that discovery point: including <JaszczurHAL.h> causes
 * arduino-cli to add the library's src/ directory to the compiler include path,
 * which in turn makes all sub-headers (hal/hal.h, hal/hal_gpio.h, …) reachable.
 *
 * Project code should use:
 *   #include <JaszczurHAL.h>
 * rather than the internal:
 *   #include <hal/hal.h>
 */

#include "hal/hal.h"
