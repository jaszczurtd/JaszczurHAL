#include <hal/hal_app.h>
#include <hal/hal_config.h>
#include <hal/hal_net.h>
#include <hal/hal_status.h>
#include <hal/hal_system.h>
#include <hal/hal_wifi.h>
#include <tools_c.h>

#include <stdint.h>

static hal_status_t s_status = HAL_NONE;
static uint32_t s_last_report_ms;

void app_start(void) {
  debugInit();
  s_status = hal_wifi_set_mode_ex(HAL_WIFI_MODE_STA);
}

void app_task0(void) {
  if (s_status == HAL_OK) {
    s_status = hal_net_service();
  }
  const uint32_t now = hal_millis();
  if (now - s_last_report_ms >= 1000u) {
    s_last_report_ms = now;
    deb("JHBT1-WIFI status=%s", hal_status_to_string(s_status));
  }
  hal_delay_ms(1u);
}
