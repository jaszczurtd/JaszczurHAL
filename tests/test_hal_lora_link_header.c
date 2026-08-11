#include "hal/radio/hal_lora_link.h"

int main(void) {
  hal_lora_link_t link = 0;
  hal_lora_link_config_t config = {0};
  hal_lora_link_send_status_t status = {HAL_LORA_OPERATION_IDLE, HAL_NONE, 0u,
                                        0u, 0u};
  (void)link;
  (void)config;
  (void)status;
  return 0;
}
