/**
 * @file app.c
 * @brief Portable software-serial loopback/echo example.
 */

#include <hal/hal_app.h>
#include <hal/hal_swserial.h>
#include <hal/hal_system.h>
#include <hal/hal_target.h>
#include <stdio.h>
#include <tools_c.h>

#if HAL_TARGET_IS_RP2040
#define EXAMPLE_SWSERIAL_RX_PIN 5u
#define EXAMPLE_SWSERIAL_TX_PIN 4u
#else
/* STM32 pin id = port * 16 + pin: PA5 / PA4. */
#define EXAMPLE_SWSERIAL_RX_PIN 5u
#define EXAMPLE_SWSERIAL_TX_PIN 4u
#endif

#ifndef EXAMPLE_SWSERIAL_BAUD
#define EXAMPLE_SWSERIAL_BAUD 9600u
#endif

static hal_swserial_t s_swserial = NULL;
static uint32_t s_last_tx_ms = 0u;
static uint32_t s_line_counter = 0u;

void app_start(void) {
  debugInit();
  deb("");
  deb("=== JaszczurHAL software serial ===");
  deb("RX=%u TX=%u baud=%lu", (unsigned)EXAMPLE_SWSERIAL_RX_PIN,
      (unsigned)EXAMPLE_SWSERIAL_TX_PIN, (unsigned long)EXAMPLE_SWSERIAL_BAUD);

  s_swserial =
      hal_swserial_create(EXAMPLE_SWSERIAL_RX_PIN, EXAMPLE_SWSERIAL_TX_PIN);
  if (s_swserial == NULL) {
    derr("SWSERIAL create FAILED");
    return;
  }

  hal_swserial_begin(s_swserial, EXAMPLE_SWSERIAL_BAUD, HAL_UART_CFG_8N1);
  hal_swserial_println(s_swserial, "JaszczurHAL swserial ready");
}

void app_task0(void) {
  if (s_swserial == NULL) {
    hal_delay_ms(1000u);
    return;
  }

  while (hal_swserial_available(s_swserial) > 0) {
    const int c = hal_swserial_read(s_swserial);
    if (c >= 0) {
      const uint8_t echo[] = {(uint8_t)c};
      hal_swserial_write(s_swserial, echo, sizeof(echo));
    }
  }

  const uint32_t now = hal_millis();
  if ((now - s_last_tx_ms) >= 2000u) {
    s_last_tx_ms = now;

    char line[48] = {};
    snprintf(line, sizeof(line), "swserial line %lu",
             (unsigned long)s_line_counter++);
    hal_swserial_println(s_swserial, line);
    deb("SWSERIAL TX: %s", line);
  }
}
