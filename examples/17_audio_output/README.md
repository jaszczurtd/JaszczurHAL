<a id="17---audio-output"></a>

# 17 - Audio generation and volume control

This example generates audio on a PWM output and controls a PGA2311 stereo
gain stage over SPI. An ADC reading sets the generated signal's frequency.
The PGA2311 starts unmuted and the application then cycles through gain
settings. It does not demonstrate toggling mute.

The PWM output uses DACless. The `use_dma` field of `hal_dacless_config_t`
selects DMA transfers or polling in the application configuration; it does
not require a separate firmware build. `use_dma` is set to `true` by default.

| Signal | RP family | NUCLEO-G474RE |
|---|---|---|
| SPI MISO / MOSI / SCK | GP16 / GP19 / GP18 | PA6 / PA7 / PA5, CN10 pins 13 / 15 / 11 (D12 / D11 / D13) |
| PGA2311 CS | GP17 | PB6, CN10 pin 17 (D10) |

On NUCLEO-G474RE, PWM audio uses PB0 (CN7 pin 34 / A3), and the ADC input
uses PA0 (A0). PWM and ADC pin assignments for other targets are in `app.c`.
