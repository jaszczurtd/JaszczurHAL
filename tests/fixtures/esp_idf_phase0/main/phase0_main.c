#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_log.h"

static const char *TAG = "jh_phase0";

void app_main(void) {
  esp_chip_info_t chip_info;

  esp_chip_info(&chip_info);
  ESP_LOGI(TAG, "ESP-IDF %s, %u core(s), silicon revision %u",
           esp_get_idf_version(), (unsigned int)chip_info.cores,
           (unsigned int)chip_info.revision);
}
