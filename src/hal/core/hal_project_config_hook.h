#pragma once

/**
 * @file hal_project_config_hook.h
 * @brief Load the optional project configuration before target selection.
 *
 * This is an early preprocessor hook. Keep hal_project_config.h macro-only and
 * do not include JaszczurHAL headers from it: target selection has not run yet.
 */

#if defined(__has_include)
#if __has_include("hal_project_config.h")
#include "hal_project_config.h"
#endif
#endif
