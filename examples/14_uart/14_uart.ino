#include <JaszczurHAL.h>

static const uint8_t UART_RX_PIN = 5;
static const uint8_t UART_TX_PIN = 4;
static const uint32_t UART_BAUD = 115200;

static hal_uart_t uart = NULL;
static uint32_t last_tx_ms = 0;
static uint32_t line_counter = 0;

void setup() {
  hal_debug_init(115200);

  uart = hal_uart_create(HAL_UART_PORT_2, UART_RX_PIN, UART_TX_PIN);
  if (!uart) {
    hal_derr("UART create failed");
    return;
  }

  hal_uart_begin(uart, UART_BAUD, HAL_UART_CFG_8N1);
  hal_uart_println(uart, "JaszczurHAL UART ready");
}

void loop() {
  if (!uart) {
    hal_delay_ms(1000);
    return;
  }

  while (hal_uart_available(uart) > 0) {
    const int c = hal_uart_read(uart);
    if (c >= 0) {
      uint8_t echo[1] = {(uint8_t)c};
      hal_uart_write(uart, echo, sizeof(echo));
    }
  }

  const uint32_t now = hal_millis();
  if (now - last_tx_ms >= 2000u) {
    last_tx_ms = now;

    char line[48] = {0};
    snprintf(line, sizeof(line), "uart line %lu",
             (unsigned long)line_counter++);
    hal_uart_println(uart, line);
    hal_deb("UART TX: %s", line);
  }
}
