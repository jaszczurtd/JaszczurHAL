# 17 - Audio output

This consolidated project compiles and exercises both output paths previously
shown by `28_pga2311` and `44_dacless_audio`:

- PGA2311 stereo gain and mute control over SPI;
- PWM audio generation with ADC-controlled frequency and the DMA-capable
  DACless service path.

Polling is a runtime DACless configuration (`DAClessConfig::useDma`), so it no
longer consumes a second, functionally identical firmware build in the gate.

RP targets use SPI0 on GP16/GP19/GP18 with CS GP17. STM32G474 uses SPI2 on
PB14/PB15/PB13 with CS PB12, leaving PA6 available for the PWM audio output.
