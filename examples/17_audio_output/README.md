<a id="17---audio-output"></a>

# 17 - Audio generation and volume control

This example generates audio on a PWM output and controls a PGA2311 stereo
gain stage over SPI. An ADC reading sets the generated signal's frequency.
The PGA2311 starts unmuted and the application then cycles through gain
settings. It does not demonstrate toggling mute.

The PWM output uses DACless. The `use_dma` field of `hal_dacless_config_t`
selects DMA transfers or polling in the application configuration; it does
not require a separate firmware build. `use_dma` is set to `true` by default.

`device_tree.json` describes the wiring of every assembly, and the program
takes its SPI bus, pins, PGA2311 SPI settings (1 MHz) and the 12-bit PWM
resolution from the generated header. The table below repeats that wiring.

| Signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| SPI MISO | GP16 | PA6, CN10 pin 13 / D12 | GPIO13 |
| SPI MOSI | GP19 | PA7, CN10 pin 15 / D11 | GPIO11 |
| SPI SCK | GP18 | PB3, CN10 pin 31 / D3 | GPIO12 |
| PGA2311 CS | GP17 | PB6, CN10 pin 17 / D10 | GPIO14 |
| PWM audio output | GP6 | PB0, CN7 pin 34 / A3 | GPIO15 |
| ADC input | GP26 | PA0, CN7 pin 28 / A0 | GPIO4 |

On NUCLEO-G474RE the SPI clock uses PB3 rather than D13 (PA5), because PA5
drives the LD2 LED. On ESP32-S3 the ADC input must be an ADC1 pin, GPIO1 to
GPIO10.
