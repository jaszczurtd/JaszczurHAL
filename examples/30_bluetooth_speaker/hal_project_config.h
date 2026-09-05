#pragma once

#define HAL_ENABLE_BLUETOOTH_A2DP_SINK
#define HAL_ENABLE_DMA_PWM_AUDIO
#define HAL_ENABLE_KV
#define HAL_BLUETOOTH_CLASSIC_MAX_PEERS 1

/* SBC decoding and flash-backed bonding exceed a safe 2 KiB core-0 margin. */
#define HAL_RP_CORE0_STACK_SIZE 4096
#define HAL_RP_CORE1_STACK_SIZE 2048

/* Two flash sectors are required for power-loss-safe KV bank publication. */
#ifndef HAL_RP_FLASH_EEPROM_SIZE
#define HAL_RP_FLASH_EEPROM_SIZE 8192
#endif
