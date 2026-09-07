<a id="07---display-and-media"></a>

# 07 - ILI9341 graphics and PNG/JPEG images

This example draws text and shapes and displays PNG and JPEG images on an
ILI9341 screen. It also shows how to encode a small PNG, convert data to and
from Base64, and prepare RGB565 pixels for display.

JPEG support uses TJpgDec and is decode-only. LodePNG provides PNG encoding.
The project enables `HAL_ENABLE_ILI9341`, `HAL_DISPLAY_ILI9341`,
`HAL_ENABLE_PNG_AS_BASE64`, and `HAL_ENABLE_JPEG_AS_BASE64`.

## Wiring

The application uses SPI bus 0.

### NUCLEO-G474RE

The table uses the connector labels printed on the board. It lists the ST
morpho pins and their electrically equivalent Arduino Uno V3 pins where
available. See Figure 18 and Table 16 in the
[STM32G4 Nucleo-64 user manual (UM2505)](https://www.st.com/resource/en/user_manual/um2505-stm32g4-nucleo64-boards-mb1367-stmicroelectronics.pdf)
for connector orientation and numbering.

| ILI9341 module signal | STM32G474RE signal | ST morpho connection | Arduino Uno V3 alternative |
|---|---|---|---|
| `SCK` / `CLK` | `PA5` (`SPI1_SCK`) | `CN10` pin 11 | `CN5` pin 6 (`D13`) |
| `MOSI` / `SDI` / `SDA` | `PA7` (`SPI1_MOSI`) | `CN10` pin 15 | `CN5` pin 4 (`D11`) |
| `MISO` / `SDO` | `PA6` (`SPI1_MISO`) | `CN10` pin 13 | `CN5` pin 5 (`D12`) |
| `CS` | `PB6` | `CN10` pin 17 | `CN5` pin 3 (`D10`) |
| `DC` / `RS` / `A0` | `PC7` | `CN10` pin 19 | `CN5` pin 2 (`D9`) |
| `RST` / `RESET` | `PA9` | `CN10` pin 21 | `CN5` pin 1 (`D8`) |
| `GND` | GND | `CN10` pin 20 | `CN6` pin 6 or 7 |
| `VCC` | 3.3 V | `CN7` pin 16 | `CN6` pin 4 (`3V3`) |
| `LED` / `BL` | 3.3 V through 100 ohm | `CN7` pin 16 | `CN6` pin 4 (`3V3`) |

The application only writes to the display, so `MISO` / `SDO` can remain
unconnected. SPI and control signals are grouped on CN10; the Arduino
alternatives use the standard SPI pins plus D10, D9, and D8. PA5 is also
connected to the on-board LD2 LED, which may flicker during SPI transfers.

GPIO signals use 3.3 V logic. Do not connect 5 V logic outputs to them.
If the display module has its own regulator or backlight resistor, follow
its schematic rather than bypassing those components.

### RP family

`CS` uses GPIO 17, `DC` GPIO 20, and `RESET` GPIO 21. Connect the panel's
clock and data signals to the SPI bus-0 pins selected for the target.

## Memory limits

Embedded images are limited to 4096 encoded bytes and 64×64 decoded pixels.
Size calculations are checked before memory allocation. PNG decoding needs
a temporary RGBA8888 buffer and a shared 8 KiB RGB565 buffer. JPEG decoding
uses the same RGB565 buffer.

These limits keep the example within the STM32G474 RAM budget; full-screen
images are rejected. Larger images need tiled processing, streaming, or
external RAM.
