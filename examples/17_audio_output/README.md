# 17 - Audio output

This consolidated project compiles and exercises both output paths previously
shown by `28_pga2311` and `44_dacless_audio`:

- PGA2311 stereo gain and mute control over SPI;
- PWM audio generation with ADC-controlled frequency and the DMA-capable
  DACless service path.

Polling is a runtime DACless configuration (`DAClessConfig::useDma`), so it no
longer consumes a second, functionally identical firmware build in the gate.

RP targets use SPI0 on GP16/GP19/GP18 with CS GP17. NUCLEO-G474RE uses SPI1 on
PA6/PA7/PA5 with CS PB6: CN10 pins 13/15/11/17, equivalent to
D12/D11/D13/D10. Its PWM audio output moves to PB0 (CN7 pin 34 / A3), while
the ADC input remains on PA0 (A0).
