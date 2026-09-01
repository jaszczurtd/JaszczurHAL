# 07 - Wyświetlacz i media

Ten przykład łączy demonstracje grafiki ILI9341 i zasobów firmware, które
wcześniej wymagały pięciu osobnych kompilacji.

| Poprzedni przykład | Zakres w tym projekcie |
|---|---|
| `09_display_tft` | Inicjalizacja ILI9341, tekst, linie, prostokąty, prostokąty zaokrąglone i okręgi. |
| `36_lodePNG` | Obraz RGBA 2x2 jest kodowany do PNG, dekodowany, kodowany Base64, ponownie dekodowany i konwertowany do RGB565. |
| `37_lodePNG_ili9341_base64` | Osadzony PNG Base64 jest sprawdzany, dekodowany, konwertowany do RGB565 i rysowany na TFT. |
| `40_jpeg` | Osadzony obraz JPEG w profilu podstawowym jest dekodowany bezpośrednio oraz za pomocą funkcji pomocniczej Base64. |
| `41_jpeg_ili931_base64` | Zdekodowane piksele JPEG RGB565 są rysowane na TFT. |

Zarządzana integracja JPEG używa TJpgDec i obsługuje wyłącznie dekodowanie.
Za kodowanie PNG odpowiada LodePNG.

Włączone funkcje:

- `HAL_ENABLE_ILI9341` i `HAL_DISPLAY_ILI9341`;
- `HAL_ENABLE_PNG_AS_BASE64`;
- `HAL_ENABLE_JPEG_AS_BASE64`.

## Połączenia

Aplikacja używa magistrali SPI 0.

### NUCLEO-G474RE

Tabela używa oznaczeń złączy nadrukowanych na PCB NUCLEO-G474RE. Najpierw
podano złącze ST morpho, a tam, gdzie jest dostępne, również elektrycznie
równoważny pin Arduino Uno V3. Orientacja i numeracja odpowiadają rysunkowi 18 i
tabeli 16 w [instrukcji płytki STM32G4 Nucleo-64 (UM2505)](https://www.st.com/resource/en/user_manual/um2505-stm32g4-nucleo64-boards-mb1367-stmicroelectronics.pdf).

| Sygnał modułu ILI9341 | Sygnał STM32G474RE | Złącze ST morpho | Alternatywa Arduino Uno V3 |
|---|---|---|---|
| `SCK` / `CLK` | `PA5` (`SPI1_SCK`) | pin 11 `CN10` | pin 6 `CN5` (`D13`) |
| `MOSI` / `SDI` / `SDA` | `PA7` (`SPI1_MOSI`) | pin 15 `CN10` | pin 4 `CN5` (`D11`) |
| `MISO` / `SDO` | `PA6` (`SPI1_MISO`) | pin 13 `CN10` | pin 5 `CN5` (`D12`) |
| `CS` | `PB6` | pin 17 `CN10` | pin 3 `CN5` (`D10`) |
| `DC` / `RS` / `A0` | `PC7` | pin 19 `CN10` | pin 2 `CN5` (`D9`) |
| `RST` / `RESET` | `PA9` | pin 21 `CN10` | pin 1 `CN5` (`D8`) |
| `GND` | GND | pin 20 `CN10` | pin 6 lub 7 `CN6` |
| `VCC` | 3,3 V | pin 16 `CN7` | pin 4 `CN6` (`3V3`) |
| `LED` / `BL` | 3,3 V przez 100 omów | pin 16 `CN7` | pin 4 `CN6` (`3V3`) |

Przykład tylko zapisuje do wyświetlacza, dlatego `MISO` / `SDO` może pozostać
niepodłączone. Sygnały SPI i sterujące są zgrupowane na `CN10`; odpowiedniki
Arduino to standardowe piny SPI oraz `D10`, `D9` i `D8`. `PA5` jest też
połączony z LED-em użytkownika `LD2`, który może migotać podczas transmisji SPI.
Sygnały GPIO pracują z poziomami logicznymi 3,3 V; nie podłączaj do nich wyjścia
logicznego 5 V. Jeżeli
moduł ma własny regulator lub rezystor podświetlenia, postępuj zgodnie ze
schematem modułu i nie omijaj tych elementów.

### Rodzina RP

Targety RP używają GPIO 17 jako `CS`, GPIO 20 jako `DC` i GPIO 21 jako `RESET`.
Połącz linie zegara i danych SPI panelu z pinami magistrali SPI 0 wybranymi przez
implementację danego targetu.

## Weryfikacja zegara STM32G474

Wariant dla NUCLEO-G474RE został sprawdzony sprzętowo z konfiguracją zegarów
backendu HSI16/PLL 170 MHz. SPI1 jest taktowane przez PCLK2 170 MHz; przykład żąda
24 MHz, a preskaler sprzętowy wybiera 21,25 MHz (`170 MHz / 8`) zamiast 8 MHz
osiąganych przy wcześniejszym starcie wyłącznie z HSI16.

Dla tego samego firmware i podłączonego ILI9341 czas zmierzony przez DWT - od
wejścia do `app_start()` do zakończenia pierwszego wywołania `app_task0()` -
skrócił się z 1,338830 s do 0,838869 s. Pomiar obejmuje całą inicjalizację,
obsługę mediów i wyświetlacza, a nie wyłącznie przepustowość SPI.

## Limity pamięci

Zasoby firmware są ograniczone do 4096 bajtów zakodowanych danych i 64 x 64
zdekodowanych pikseli. Wszystkie obliczenia rozmiaru są sprawdzane przed
alokacją. Dekodowanie PNG używa tymczasowej alokacji RGBA8888 i wspólnego bufora
RGB565 8 KiB; JPEG ponownie wykorzystuje ten sam bufor RGB565. Limity pozwalają
zmieścić przykład w RAM STM32G474 i celowo odrzucają zasoby pełnoekranowe.
Większe aplikacje powinny przetwarzać obraz kafelkami lub strumieniowo albo
korzystać z zewnętrznej pamięci RAM.
