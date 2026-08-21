#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/core/jh_handle_pool.h"
#include "jh_board_config.h"
#include "jh_link_contract.h"

static const char *TAG = "jh_phase0";

void app_start(void) {
  esp_chip_info_t chip_info;
  jh_handle_pool_t pool;
  jh_handle_slot_t slots[1];
  int token = 0;
  void *handle = NULL;
  void *resolved_token = NULL;

  JH_BOARD_CONTRACT_SYMBOL();
  ESP_ERROR_CHECK(
      jh_handle_pool_init(&pool, slots, 1u, 1u) == HAL_OK ? ESP_OK : ESP_FAIL);
  ESP_ERROR_CHECK(
      jh_handle_allocate(&pool, &token, &handle) == HAL_OK ? ESP_OK : ESP_FAIL);
  ESP_ERROR_CHECK(jh_handle_resolve(&pool, handle, &resolved_token, NULL) ==
                              HAL_OK &&
                          resolved_token == &token
                      ? ESP_OK
                      : ESP_FAIL);

  esp_chip_info(&chip_info);
  ESP_LOGI(TAG,
           "JaszczurHAL component handoff for %s, ESP-IDF %s, %u core(s), "
           "silicon revision %u",
           HAL_TARGET_DESCRIPTOR_ID, esp_get_idf_version(),
           (unsigned int)chip_info.cores, (unsigned int)chip_info.revision);
}

void app_task0(void) { vTaskDelay(pdMS_TO_TICKS(1000)); }
