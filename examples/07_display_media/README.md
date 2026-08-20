# 07 - Display and media

This example combines the ILI9341 graphics and firmware-asset demonstrations
that previously required five separate builds.

| Previous example | Coverage in this project |
|---|---|
| `09_display_tft` | ILI9341 initialization, text, lines, rectangles, rounded rectangles, and circles. |
| `36_lodePNG` | A 2x2 RGBA image is encoded to PNG, decoded, Base64-encoded, decoded again, and converted to RGB565. |
| `37_lodePNG_ili9341_base64` | An embedded Base64 PNG is inspected, decoded, converted to RGB565, and drawn on the TFT. |
| `40_jpeg` | An embedded baseline JPEG is decoded through both the direct and Base64 helper paths. |
| `41_jpeg_ili931_base64` | The decoded JPEG RGB565 pixels are drawn on the TFT. |

The managed JPEG integration uses TJpgDec and is decode-only. PNG encoding is
provided by LodePNG.

Enabled features:

- `HAL_ENABLE_ILI9341` and `HAL_DISPLAY_ILI9341`;
- `HAL_ENABLE_PNG_AS_BASE64`;
- `HAL_ENABLE_JPEG_AS_BASE64`.

## Wiring

The application uses SPI bus 0.

### NUCLEO-G474RE

The following table names the connectors printed on the NUCLEO-G474RE PCB.
The ST morpho connection is listed first; the electrically equivalent Arduino
Uno V3 header pin is included where one is available. Connector orientation
and numbering follow Figure 18 and Table 16 in the
[STM32G4 Nucleo-64 board user manual (UM2505)](https://www.st.com/resource/en/user_manual/um2505-stm32g4-nucleo64-boards-mb1367-stmicroelectronics.pdf).

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

This example only writes to the display, so `MISO` / `SDO` may remain
unconnected. The SPI and control signals are grouped on `CN10`; the equivalent
Arduino connections are the standard SPI pins plus `D10`, `D9`, and `D8`.
`PA5` is also connected to the on-board user LED (`LD2`), which may flicker
during SPI transfers. The GPIO signals use 3.3 V logic; never connect a 5 V
logic output to them. If the display module includes its own regulator or
backlight resistor, follow the module schematic instead of bypassing those
components.

### RP family

RP-family targets use GPIO 17 for `CS`, GPIO 20 for `DC`, and GPIO 21 for
`RESET`. Connect the panel's SPI clock and data pins to the SPI bus-0 pins
selected by the target backend.

## STM32G474 clock validation

The NUCLEO-G474RE path has been validated on hardware with the backend's
170 MHz HSI16/PLL clock tree. SPI1 is sourced from the 170 MHz PCLK2; the
example requests 24 MHz and the hardware prescaler selects 21.25 MHz
(`170 MHz / 8`), up from 8 MHz with the former HSI16-only startup.

For the same firmware and connected ILI9341, a DWT measurement from entry to
`app_start()` through the first `app_task0()` call improved from 1.338830 s to
0.838869 s. This is an end-to-end initialization/media/display measurement,
not a pure SPI throughput benchmark.

## Memory limits

Firmware assets are limited to 4096 encoded bytes and 64 x 64 decoded pixels.
All decoded-size calculations are checked before allocation. PNG decode uses a
temporary RGBA8888 allocation and a shared 8 KiB RGB565 buffer; JPEG reuses the
same RGB565 buffer. The limits keep the example inside the STM32G474 RAM budget
and intentionally reject full-screen assets. Larger applications should use
tiling, streaming, or external RAM.
