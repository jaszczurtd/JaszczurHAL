#pragma once

/* Targets this example builds for. */
#define JH_PROJECT_TARGETS(X)                                                  \
  X(HAL_TARGET_RP2040)                                                         \
  X(HAL_TARGET_STM32G474)

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(PROBE, "LoRa radio checks that do not transmit",                           \
    HAL_LORA_EXAMPLE_PROBE_ONLY = 1)                                           \
  X(RESPONDER, "LoRa ping/pong responder", HAL_LORA_EXAMPLE_RESPONDER = 1)     \
  X(SF7, "LoRa initiator with the SF7/6 dBm test settings",                    \
    HAL_LORA_EXAMPLE_SF = 7, HAL_LORA_EXAMPLE_TX_POWER_DBM = 6)                \
  X(RESPONDER_SF7, "LoRa responder with the SF7/6 dBm test settings",          \
    HAL_LORA_EXAMPLE_RESPONDER = 1, HAL_LORA_EXAMPLE_SF = 7,                   \
    HAL_LORA_EXAMPLE_TX_POWER_DBM = 6)                                         \
  X(LINK, "LoRa echo-command initiator with fragmentation and retries",        \
    HAL_ENABLE_LORA_COMMANDS = 1)                                              \
  X(LINK_RESPONDER,                                                            \
    "LoRa echo-command responder with fragmentation and retries",              \
    HAL_ENABLE_LORA_COMMANDS = 1, HAL_LORA_LINK_EXAMPLE_RESPONDER = 1)

#define HAL_ENABLE_SX126X
