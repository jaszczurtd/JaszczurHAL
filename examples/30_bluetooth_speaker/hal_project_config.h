#pragma once

#define HAL_ENABLE_BLUETOOTH_A2DP_SINK
#define HAL_ENABLE_DMA_PWM_AUDIO
#define HAL_ENABLE_KV
#define HAL_BLUETOOTH_CLASSIC_MAX_PEERS 1

/* SBC decoding and saving pairing data to flash need more margin than a 2 KiB
 * core-0 stack. */
#define HAL_RP_CORE0_STACK_SIZE 4096
#define HAL_RP_CORE1_STACK_SIZE 2048

/* Reserve two flash sectors so KV can commit a new bank without overwriting the
 * previous one first. */
#ifndef HAL_RP_FLASH_EEPROM_SIZE
#define HAL_RP_FLASH_EEPROM_SIZE 8192
#endif
