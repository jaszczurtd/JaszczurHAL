#pragma once

#define HAL_ENABLE_UART
/* The Z command overflows the main stack into the guard. */
#define HAL_ENABLE_STACK_GUARD 1

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif
