<a id="14---mcp2515-can"></a>

# 14 - Sending and receiving CAN frames with MCP2515

This example sends a CAN frame with ID `0x321` once per second and prints
received frames to the serial console. The MCP2515 connects to SPI bus 0.
The application polls for received data, so no interrupt pin is needed.

`HAL_ENABLE_MCP2515` enables the driver and its CAN and SPI dependencies.
The wiring below covers RP2040 and STM32G474.

<a id="rp2040"></a>

<a id="stm32g474"></a>

## Wiring

| MCP2515 signal | RP2040 | STM32G474 / NUCLEO-G474RE |
|---|---|---|
| MISO | GPIO16 | PA6, CN10 pin 13 / D12 |
| MOSI | GPIO19 | PA7, CN10 pin 15 / D11 |
| SCK | GPIO18 | PA5, CN10 pin 11 / D13 |
| CS | GPIO17 | PB6, CN10 pin 17 / D10 |

Use an MCP2515 module with a CAN transceiver and a terminated CAN bus.
`hal_can_create()` enables one-shot transmission: a missing ACK causes the
send attempt to fail rather than retry indefinitely. A controller on an
otherwise disconnected bus will not receive that acknowledgement.
