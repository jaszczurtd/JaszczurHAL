#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/core/hal_app.h"
#include "hal/core/hal_target.h"
#include "jh_board_config.h"
#include "jh_link_contract.h"

#include <stdio.h>

#if !HAL_TARGET_IS_ESP32_S3
#error "The ESP32-S3 Phase 3 fixture requires the exact esp32s3 target"
#endif

#if !HAL_BOARD_IS_WAVESHARE_ESP32_S3_ZERO
#error "The ESP32-S3 Phase 3 fixture requires waveshare-esp32-s3-zero"
#endif

void jh_phase3_link_probe(void);

void app_start(void) {
  JH_BOARD_CONTRACT_SYMBOL();
  jh_phase3_link_probe();
  printf("JH_ESP32_PHASE3_COMPILE target=%s board=%s\n",
         HAL_TARGET_DESCRIPTOR_ID, HAL_BOARD_PROFILE_NAME);
}

void app_task0(void) { vTaskDelay(pdMS_TO_TICKS(1000u)); }

void app_task1(void) { vTaskDelay(pdMS_TO_TICKS(1000u)); }
