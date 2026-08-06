# 14 - MCP2515 CAN

Portable MCP2515 example for both RP2040 and STM32G474.

What it does:
- initializes SPI bus 0
- initializes one MCP2515 controller on the configured CS pin
- sends one heartbeat frame per second with CAN ID `0x321`
- polls RX and prints any received frames to serial output

This example uses polling only. No interrupt pin is required.
It enables the MCP2515 backend with `HAL_ENABLE_MCP2515`, which pulls in the
generic CAN facade and SPI dependency.

## Wiring

### RP2040

- MISO: GPIO16
- MOSI: GPIO19
- SCK: GPIO18
- CS: GPIO17

### STM32G474

- MISO: PA6
- MOSI: PA7
- SCK: PA5
- CS: PA4

Use an MCP2515 board with a CAN transceiver and a properly terminated CAN bus.
Because `hal_can_create()` enables one-shot TX, a missing ACK on an otherwise
disconnected bus will make the send attempt fail instead of retrying forever.
