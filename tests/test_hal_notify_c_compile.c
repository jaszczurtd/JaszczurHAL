#include "hal/network/notify/hal_notify.h"

int main(void) {
  hal_notify_config_t config;
  hal_notify_message_t message;
  hal_notify_receipt_t receipt = {0};
  (void)hal_notify_config_init(&config);
  (void)hal_notify_message_init(&message);
  config.device_name = "c-device";
  message.device_name = config.device_name;
  receipt.parts_total = 1u;
  (void)receipt;
#ifdef HAL_ENABLE_NOTIFY_TELEGRAM
  hal_notify_telegram_config_t telegram;
  (void)hal_notify_telegram_config_init(&telegram);
  config.backend = hal_notify_telegram_backend();
#endif
  return 0;
}
