#pragma once

/**
 * @file hal_project_config_hook.h
 * @brief Load the optional project configuration before target selection.
 *
 * This is an early preprocessor hook. Keep hal_project_config.h macro-only and
 * do not include JaszczurHAL headers from it: target selection has not run yet.
 *
 * A build with a device tree passes its generated jh_hardware.h as
 * JH_HARDWARE_HEADER, so the project configuration sees the JH_HW_* macros of
 * the selected assembly. Only that named file is loaded; a jh_hardware.h found
 * elsewhere on the include path never enables this.
 */

#if defined(JH_HARDWARE_HEADER)
#include JH_HARDWARE_HEADER
#endif

#if defined(__has_include)
#if __has_include("hal_project_config.h")
#include "hal_project_config.h"
#endif
#endif
