<a id="14---mcp2515-can"></a>

# 14 - Wysyłanie i odbieranie ramek CAN przez MCP2515

Przykład co sekundę wysyła ramkę CAN o identyfikatorze `0x321` i wypisuje
odebrane ramki w konsoli szeregowej. Kontroler MCP2515 jest podłączony do
magistrali SPI 0. Aplikacja cyklicznie sprawdza odbiór, więc nie wymaga
podłączenia pinu przerwania. Wbudowana dioda zmienia stan przy każdej wysłanej
ramce i gaśnie, gdy wysłanie się nie uda.

Flaga `HAL_ENABLE_MCP2515` włącza sterownik oraz potrzebną obsługę CAN i SPI.
Połączenia każdego montażu opisuje `device_tree.json`, a program bierze
magistralę, piny i ustawienia CAN z wygenerowanego nagłówka. Poniższa tabela
powtarza te połączenia.

<a id="rp2040"></a>

<a id="stm32g474"></a>

## Połączenia

| Sygnał MCP2515 | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| MISO | GPIO16 | PA6, pin 13 CN10 / D12 | GPIO6 |
| MOSI | GPIO19 | PA7, pin 15 CN10 / D11 | GPIO7 |
| SCK | GPIO18 | PB3, pin 31 CN10 / D3 | GPIO5 |
| CS | GPIO17 | PB6, pin 17 CN10 / D10 | GPIO4 |

Na NUCLEO-G474RE zegar SPI idzie przez PB3 zamiast D13 (PA5), bo PA5 steruje
diodą LD2, która pokazuje wysłane ramki.

Użyj modułu MCP2515 z transceiverem CAN i rezystorami terminującymi
magistralę. `hal_can_create()` włącza tryb pojedynczej próby nadawania:
brak potwierdzenia ACK kończy wysyłanie statusem `HAL_EIO` (widocznym
w logu), a nie nieograniczonym ponawianiem. Sam kontroler na odłączonej magistrali nie otrzyma takiego
potwierdzenia.
