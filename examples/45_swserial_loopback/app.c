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

  hal_status_t status = hal_swserial_create_ex(
      EXAMPLE_SWSERIAL_RX_PIN, EXAMPLE_SWSERIAL_TX_PIN, &s_swserial);
  if (status != HAL_OK) {
    derr("SWSERIAL create FAILED: %s", hal_status_to_string(status));
    return;
  }

  status =
      hal_swserial_begin(s_swserial, EXAMPLE_SWSERIAL_BAUD, HAL_UART_CFG_8N1);
  if (status != HAL_OK) {
    derr("SWSERIAL begin FAILED: %s", hal_status_to_string(status));
    hal_swserial_destroy(s_swserial);
    s_swserial = NULL;
    return;
  }
  status =
      hal_swserial_println_ex(s_swserial, "JaszczurHAL swserial ready", NULL);
  if (status != HAL_OK) {
    derr("SWSERIAL initial write FAILED: %s", hal_status_to_string(status));
    hal_swserial_destroy(s_swserial);
    s_swserial = NULL;
  }
}

void app_task0(void) {
  if (s_swserial == NULL) {
    hal_delay_ms(1000u);
    return;
  }

  while (hal_swserial_available(s_swserial) > 0) {
    uint8_t value = 0u;
    hal_status_t status = hal_swserial_read_ex(s_swserial, &value);
    if (status == HAL_EAGAIN) {
      break;
    }
    if (status != HAL_OK) {
      derr("SWSERIAL read FAILED: %s", hal_status_to_string(status));
      break;
    }

    size_t written = 0u;
    status = hal_swserial_write_ex(s_swserial, &value, 1u, &written);
    if (status != HAL_OK || written != 1u) {
      derr("SWSERIAL echo FAILED: %s (written=%lu)",
           hal_status_to_string(status), (unsigned long)written);
      break;
    }
  }

  const uint32_t now = hal_millis();
  if ((now - s_last_tx_ms) >= 2000u) {
    s_last_tx_ms = now;

    char line[48] = {};
    snprintf(line, sizeof(line), "swserial line %lu",
             (unsigned long)s_line_counter++);
    const hal_status_t status = hal_swserial_println_ex(s_swserial, line, NULL);
    if (status == HAL_OK) {
      deb("SWSERIAL TX: %s", line);
    } else {
      derr("SWSERIAL TX FAILED: %s", hal_status_to_string(status));
    }
  }
}
