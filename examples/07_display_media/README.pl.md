<a id="07---wyświetlacz-i-media"></a>

# 07 - Grafika i obrazy PNG/JPEG na ILI9341

Przykład rysuje tekst i figury oraz wyświetla obrazy PNG i JPEG na ekranie
ILI9341. Pokazuje także kodowanie małego obrazu do PNG, konwersję Base64
oraz przygotowanie pikseli RGB565 do wyświetlenia.

Za JPEG odpowiada TJpgDec, który w tej integracji obsługuje wyłącznie
odczyt obrazów. Kodowanie PNG zapewnia LodePNG.
Projekt włącza `HAL_ENABLE_ILI9341`, `HAL_DISPLAY_ILI9341`,
`HAL_ENABLE_PNG_AS_BASE64` i `HAL_ENABLE_JPEG_AS_BASE64`.

## Połączenia

Aplikacja używa magistrali SPI 0.

### NUCLEO-G474RE

Tabela podaje oznaczenia złączy na płytce. Obok pinów ST morpho znajdują się
odpowiadające im elektrycznie piny Arduino Uno V3, jeżeli są dostępne.
Numerację i orientację złączy przedstawiają rysunek 18 i tabela 16
w [instrukcji STM32G4 Nucleo-64 (UM2505)](https://www.st.com/resource/en/user_manual/um2505-stm32g4-nucleo64-boards-mb1367-stmicroelectronics.pdf).

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

Przykład tylko wysyła dane do wyświetlacza, dlatego `MISO` / `SDO` może
pozostać niepodłączone. Sygnały SPI i sterujące są dostępne na CN10;
na złączach Arduino odpowiadają im standardowe piny SPI oraz D10, D9 i D8.
PA5 jest też połączony z diodą LD2, która może migotać podczas transmisji SPI.

Sygnały GPIO mają poziom 3,3 V. Nie podłączaj do nich wyjść logicznych 5 V.
Jeżeli moduł wyświetlacza ma własny regulator lub rezystor podświetlenia,
postępuj zgodnie z jego schematem i nie omijaj tych elementów.

### Rodzina RP

`CS` jest na GPIO 17, `DC` na GPIO 20, a `RESET` na GPIO 21.
Podłącz zegar i dane panelu do pinów SPI 0 wybranych dla danej platformy.

## Limity pamięci

Obrazy zapisane w programie mogą mieć najwyżej 4096 bajtów zakodowanych
danych i rozmiar 64×64 piksele po dekodowaniu. Rozmiary są sprawdzane przed
przydzieleniem pamięci. Dekodowanie PNG wymaga tymczasowego bufora RGBA8888
oraz wspólnego bufora RGB565 o rozmiarze 8 KiB. JPEG korzysta z tego samego
bufora RGB565.

Limity pozwalają zmieścić przykład w pamięci RAM STM32G474; obrazy
pełnoekranowe są odrzucane. Przy większych obrazach zastosuj przetwarzanie
fragmentami, odczyt strumieniowy lub zewnętrzną pamięć RAM.
