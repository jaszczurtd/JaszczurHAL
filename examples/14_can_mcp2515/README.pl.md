<a id="14---mcp2515-can"></a>

# 14 - Wysyłanie i odbieranie ramek CAN przez MCP2515

Przykład co sekundę wysyła ramkę CAN o identyfikatorze `0x321` i wypisuje
odebrane ramki w konsoli szeregowej. Kontroler MCP2515 jest podłączony do
magistrali SPI 0. Aplikacja cyklicznie sprawdza odbiór, więc nie wymaga
podłączenia pinu przerwania.

Flaga `HAL_ENABLE_MCP2515` włącza sterownik oraz potrzebną obsługę CAN i SPI.
Poniższe połączenia dotyczą RP2040 i STM32G474.

<a id="rp2040"></a>

<a id="stm32g474"></a>

## Połączenia

| Sygnał MCP2515 | RP2040 | STM32G474 / NUCLEO-G474RE |
|---|---|---|
| MISO | GPIO16 | PA6, pin 13 CN10 / D12 |
| MOSI | GPIO19 | PA7, pin 15 CN10 / D11 |
| SCK | GPIO18 | PA5, pin 11 CN10 / D13 |
| CS | GPIO17 | PB6, pin 17 CN10 / D10 |

Użyj modułu MCP2515 z transceiverem CAN i rezystorami terminującymi
magistralę. `hal_can_create()` włącza tryb pojedynczej próby nadawania:
brak potwierdzenia ACK powoduje błąd wysyłania, a nie nieograniczone
ponawianie. Sam kontroler na odłączonej magistrali nie otrzyma takiego
potwierdzenia.
