#pragma once

/* Targets this example builds for. */
#define JH_PROJECT_TARGETS(X)                                                  \
  X(HAL_TARGET_RP2040)                                                         \
  X(HAL_TARGET_RP2350_ARM)

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(AVRCP, "A2DP speaker with AVRCP volume control",                           \
    HAL_ENABLE_BLUETOOTH_AVRCP_TARGET = 1)                                     \
  X(BLE_A2DP, "A2DP speaker with BLE and Classic support in one image",        \
    HAL_ENABLE_BLE = 1, HAL_SPEAKER_EXAMPLE_ENABLE_BLE = 1)

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
