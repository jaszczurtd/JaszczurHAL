#pragma once

/* Each variant selects its features in the project manifest to avoid enabling
 * unrelated modules. */

#if defined(HAL_TARGET_RP2040) || defined(HAL_TARGET_RP2350_ARM)
/* Formatting pool and transport diagnostics uses about 3 KiB of stack.
 * Reserve 4 KiB on core 0, matching the margin used by the A2DP example.
 */
#define HAL_RP_CORE0_STACK_SIZE 4096
#define HAL_RP_CORE1_STACK_SIZE 2048
#endif
