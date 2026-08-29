# 17 - Wyjście audio

Projekt buduje i sprawdza obie ścieżki wyjściowe:

- regulację wzmocnienia stereo i wyciszania PGA2311 przez SPI;
- generowanie audio PWM z częstotliwością sterowaną przez ADC oraz ścieżkę
  usługi DACless obsługującą DMA.

Polling jest konfiguracją DACless w runtime (`DAClessConfig::useDma`), dlatego
nie wymaga już drugiego, funkcjonalnie identycznego builda firmware w gate.

Targety RP używają SPI0 na GP16/GP19/GP18 z CS GP17. NUCLEO-G474RE używa SPI1
na PA6/PA7/PA5 z CS PB6: piny 13/15/11/17 CN10, odpowiadające
D12/D11/D13/D10. Wyjście audio PWM przechodzi na PB0 (pin 34 CN7 / A3), a
wejście ADC pozostaje na PA0 (A0).
