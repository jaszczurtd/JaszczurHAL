#pragma once

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_PNG
#ifndef HAL_LODEPNG_ENABLE_DISK
#define LODEPNG_NO_COMPILE_DISK
#endif
#ifndef HAL_LODEPNG_ENABLE_CPP
#define LODEPNG_NO_COMPILE_CPP
#endif
#if defined(__cplusplus) && !defined(HAL_LODEPNG_ENABLE_CPP)
extern "C" {
#endif
#include <lodepng.h>
#if defined(__cplusplus) && !defined(HAL_LODEPNG_ENABLE_CPP)
}
#endif
#endif
