#include "hal/hal_tls.h"

int main(void) {
  hal_tls_client_config_t config;
  hal_tls_client_t client = 0;
  return hal_tls_client_config_init(&config) == HAL_OK && client == 0 ? 0 : 1;
}
