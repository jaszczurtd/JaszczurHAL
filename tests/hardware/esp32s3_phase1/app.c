#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/core/hal_app.h"
#include "hal/core/hal_target.h"
#include "jh_board_config.h"
#include "jh_link_contract.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#if !HAL_TARGET_IS_ESP32_S3
#error "The ESP32-S3 Phase 1 probe requires the exact esp32s3 target"
#endif

#if !HAL_BOARD_IS_WAVESHARE_ESP32_S3_ZERO
#error "The ESP32-S3 Phase 1 probe requires waveshare-esp32-s3-zero"
#endif

static esp_chip_info_t s_chip_info;
static uint32_t s_flash_bytes;
static size_t s_psram_bytes;
static bool s_psram_initialized;
static bool s_contract_matches;
static uint32_t s_task0_sequence;

static void report_contract(const char *phase, uint32_t sequence) {
  printf("JH_ESP32_PHASE1 phase=%s sequence=%" PRIu32
         " target=%s board=%s model_match=%u cores=%u "
         "expected_cores=%u flash=%" PRIu32 " expected_flash=%" PRIu32
         " psram_initialized=%u psram=%u expected_psram=%" PRIu32
         " status=%s\n",
         phase, sequence, HAL_TARGET_DESCRIPTOR_ID, HAL_BOARD_PROFILE_NAME,
         s_chip_info.model == CHIP_ESP32S3 ? 1u : 0u,
         (unsigned int)s_chip_info.cores, (unsigned int)HAL_TARGET_CPU_CORES,
         s_flash_bytes, (uint32_t)HAL_BOARD_EXPECTED_FLASH_BYTES,
         s_psram_initialized ? 1u : 0u, (unsigned int)s_psram_bytes,
         (uint32_t)HAL_BOARD_PSRAM_BYTES, s_contract_matches ? "PASS" : "FAIL");
  fflush(stdout);
}

void app_start(void) {
  JH_BOARD_CONTRACT_SYMBOL();

  esp_chip_info(&s_chip_info);
  const esp_err_t flash_status =
      esp_flash_get_physical_size(NULL, &s_flash_bytes);
  s_psram_initialized = esp_psram_is_initialized();
  s_psram_bytes = s_psram_initialized ? esp_psram_get_size() : 0u;

  s_contract_matches =
      s_chip_info.model == CHIP_ESP32S3 &&
      s_chip_info.cores == (uint8_t)HAL_TARGET_CPU_CORES &&
      flash_status == ESP_OK &&
      s_flash_bytes == (uint32_t)HAL_BOARD_EXPECTED_FLASH_BYTES &&
      s_psram_initialized == (HAL_BOARD_HAS_PSRAM != 0) &&
      s_psram_bytes == (size_t)HAL_BOARD_PSRAM_BYTES;

  report_contract("start", 0u);
}

void app_task0(void) {
  ++s_task0_sequence;
  report_contract("task0", s_task0_sequence);
  vTaskDelay(pdMS_TO_TICKS(1000u));
}
