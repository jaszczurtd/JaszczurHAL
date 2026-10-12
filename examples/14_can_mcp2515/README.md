<a id="14---mcp2515-can"></a>

# 14 - Sending and receiving CAN frames with MCP2515

This example sends a CAN frame with ID `0x321` once per second and prints
received frames to the serial console. The MCP2515 connects to SPI bus 0.
The application polls for received data, so no interrupt pin is needed.
The built-in LED toggles with every frame sent and goes off when a send fails.

`HAL_ENABLE_MCP2515` enables the driver and its CAN and SPI dependencies.
`device_tree.json` describes the wiring of every assembly, and the program
takes its bus, pins and CAN settings from the generated header. The table
below repeats that wiring.

<a id="rp2040"></a>

<a id="stm32g474"></a>

## Wiring

| MCP2515 signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| MISO | GPIO16 | PA6, CN10 pin 13 / D12 | GPIO6 |
| MOSI | GPIO19 | PA7, CN10 pin 15 / D11 | GPIO7 |
| SCK | GPIO18 | PB3, CN10 pin 31 / D3 | GPIO5 |
| CS | GPIO17 | PB6, CN10 pin 17 / D10 | GPIO4 |

On NUCLEO-G474RE the SPI clock uses PB3 rather than D13 (PA5), because PA5
drives the LD2 LED, which shows the sent frames.

Use an MCP2515 module with a CAN transceiver and a terminated CAN bus.
`hal_can_create()` enables one-shot transmission: a missing ACK makes the
send fail with `HAL_EIO` (printed in the log) rather than retry
indefinitely. A controller on an
otherwise disconnected bus will not receive that acknowledgement.
