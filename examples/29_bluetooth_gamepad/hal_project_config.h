#pragma once

/* Feature selection lives in the example manifest so variants stay isolated. */

#if defined(HAL_TARGET_RP2040) || defined(HAL_TARGET_RP2350_ARM)
/* Runtime diagnostics reach about 3 KiB while formatting pool and transport
 * high-water data. Keep the same measured safety margin as the A2DP example.
 */
#define HAL_RP_CORE0_STACK_SIZE 4096
#define HAL_RP_CORE1_STACK_SIZE 2048
#endif
