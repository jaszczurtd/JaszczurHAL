# 14 - MCP2515 CAN

Jest to przenośny przykład użycia MCP2515 na RP2040 i STM32G474.

Działanie:

- inicjalizuje magistralę SPI 0;
- inicjalizuje jeden kontroler MCP2515 na skonfigurowanym pinie CS;
- co sekundę wysyła ramkę sygnalizującą działanie urządzenia, o identyfikatorze
  CAN `0x321`;
- cyklicznie sprawdza kolejkę odbiorczą i wypisuje odebrane ramki przez port
  szeregowy.

Przykład cyklicznie odpytuje kontroler i nie wymaga pinu przerwania.
Włącza obsługę MCP2515 przez `HAL_ENABLE_MCP2515`, co dołącza ogólną fasadę CAN
oraz zależność SPI.

## Połączenia

### RP2040

- MISO: GPIO16
- MOSI: GPIO19
- SCK: GPIO18
- CS: GPIO17

### STM32G474

- MISO: PA6, pin 13 CN10 / D12
- MOSI: PA7, pin 15 CN10 / D11
- SCK: PA5, pin 11 CN10 / D13
- CS: PB6, pin 17 CN10 / D10

Użyj modułu MCP2515 z transceiverem CAN i prawidłowo zakończonej magistrali CAN.
Ponieważ `hal_can_create()` włącza jednorazową transmisję, brak ACK na odłączonej
magistrali spowoduje błąd wysyłania zamiast nieskończonego ponawiania.
