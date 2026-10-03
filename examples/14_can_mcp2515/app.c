/**
 * @file app.c
 * @brief Send a periodic CAN frame and print received frames using an MCP2515.
 *
 * The controller is polled over SPI; no interrupt pin is required. The
 * built-in LED toggles with every frame sent and goes off when a send fails.
 */

#include <hal/can/hal_can.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_target.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/serial/hal_serial.h>
#include <hal/spi/hal_spi.h>
#include <hal/system/hal_system.h>

#if HAL_TARGET_IS_RP
#define EXAMPLE_SPI_MISO 16u
#define EXAMPLE_SPI_MOSI 19u
#define EXAMPLE_SPI_SCK 18u
#define EXAMPLE_CAN_CS 17u
#elif HAL_TARGET_IS_STM32G474
#define EXAMPLE_SPI_MISO 6u
#define EXAMPLE_SPI_MOSI 7u
#define EXAMPLE_SPI_SCK 5u
#define EXAMPLE_CAN_CS 22u
#else
#define EXAMPLE_SPI_MISO 6u
#define EXAMPLE_SPI_MOSI 7u
#define EXAMPLE_SPI_SCK 5u
#define EXAMPLE_CAN_CS 4u
#endif

/* On NUCLEO-G474RE the LD2 LED is PA5, the SPI clock; the LED is left to it. */
#if defined(HAL_LED_BUILTIN) && HAL_LED_BUILTIN != EXAMPLE_SPI_SCK
#define EXAMPLE_LED HAL_LED_BUILTIN
#endif

static hal_can_t s_can = NULL;
static uint32_t s_counter = 0u;

static void show_send(bool sent) {
#if defined(EXAMPLE_LED)
  hal_gpio_write(EXAMPLE_LED, sent && (s_counter & 1u) == 0u);
#else
  (void)sent;
#endif
}

void app_start(void) {
  hal_debug_init_default();
  deb("");
  deb("=== JaszczurHAL MCP2515 CAN example ===");
  deb("Initialising SPI bus and MCP2515 on CS pin...");
#if defined(EXAMPLE_LED)
  hal_gpio_set_mode(EXAMPLE_LED, HAL_GPIO_OUTPUT_LOW);
#endif

  hal_spi_init(0u, EXAMPLE_SPI_MISO, EXAMPLE_SPI_MOSI, EXAMPLE_SPI_SCK);
  hal_can_config_t can_cfg = hal_can_default_config();
  can_cfg.mcp2515.cs_pin = EXAMPLE_CAN_CS;
  const hal_status_t st = hal_can_create_with_retry(
      &can_cfg, HAL_CAN_NO_INT_PIN, NULL, 2, NULL, &s_can);
  if (st == HAL_OK) {
    deb("MCP2515 init OK");
  } else {
    derr("MCP2515 init FAILED: %s", hal_status_to_string(st));
  }
}

void app_task0(void) {
  if (!s_can) {
    hal_delay_ms(1000);
    return;
  }

  uint8_t payload[8] = {};
  payload[0] = (uint8_t)(s_counter & 0xFFu);
  payload[1] = (uint8_t)((s_counter >> 8) & 0xFFu);

  /* No ACK from another node shows as HAL_EIO (one-shot) or HAL_ETIMEOUT. */
  const hal_status_t sent = hal_can_send(s_can, 0x321u, 2u, payload);
  if (sent == HAL_OK) {
    deb("TX id=0x321 seq=%lu OK", (unsigned long)s_counter);
  } else {
    derr("TX id=0x321 seq=%lu FAIL: %s", (unsigned long)s_counter,
         hal_status_to_string(sent));
  }
  show_send(sent == HAL_OK);

  hal_status_t rx_st = HAL_OK;
  while (rx_st == HAL_OK) {
    uint32_t id = 0u;
    uint8_t len = 0u;
    uint8_t rx[HAL_CAN_MAX_DATA_LEN] = {};
    rx_st = hal_can_receive(s_can, &id, &len, rx);
    if (rx_st == HAL_OK) {
      deb("RX id=%lu len=%u", (unsigned long)id, (unsigned)len);
    } else if (rx_st != HAL_EAGAIN) {
      derr("RX FAIL: %s", hal_status_to_string(rx_st));
    }
  }

  s_counter++;
  hal_delay_ms(1000);
}
