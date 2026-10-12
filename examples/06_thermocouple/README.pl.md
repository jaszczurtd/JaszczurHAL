# 06 - Odczyt temperatury z termopar

Przykład odczytuje temperaturę z MCP9600 i MAX6675 przez wspólne API
`hal_thermocouple`. Dla MCP9600 podaje temperaturę termopary i temperaturę
otoczenia układu; dla MAX6675 - temperaturę termopary. Wyniki wypisuje
co sekundę w konsoli diagnostycznej.

Układy są inicjalizowane niezależnie. Błąd inicjalizacji jednego nie blokuje
próby uruchomienia drugiego. Konfiguracja włącza oba sterowniki przez
`HAL_ENABLE_MCP9600` i `HAL_ENABLE_MAX6675` oraz obsługę I2C przez
`HAL_ENABLE_I2C`.

## Połączenia i ustawienia

Połączenia każdego montażu opisuje `device_tree.json`, a program bierze
magistralę, piny i adres z wygenerowanego nagłówka. Poniższa tabela powtarza
te połączenia.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| MCP9600 SDA / SCL | GP4 / GP5 | PB9 / PB8, piny 5 / 3 CN10 (D14 / D15) | GPIO4 / GPIO5 |
| MAX6675 SCLK | GP18 | PB3, pin 31 CN10 / D3 | GPIO18 |
| MAX6675 CS | GP17 | PB6, pin 17 CN10 / D10 | GPIO17 |
| MAX6675 MISO | GP16 | PA6, pin 13 CN10 / D12 | GPIO16 |

Na NUCLEO-G474RE zegar MAX6675 idzie przez PB3 zamiast D13 (PA5), bo PA5
steruje diodą LD2.

MCP9600 używa magistrali I2C 0, adresu `0x67` i standardowej częstotliwości
I2C. Aplikacja wybiera termoparę typu K i ustawia filtr na `2`.
Użyj termopar i modułów zgodnych z tymi ustawieniami; MAX6675 obsługuje typ K.

## Kompilacja

Z głównego katalogu repozytorium uruchom:

```bash
vscode/entry/jh-vscode build \
  --project examples/06_thermocouple --assembly pico
```

Pozostałe montaże z `device_tree.json` obejmują Pico 2 (ARM i RISC-V),
NUCLEO-G474RE i ESP32-S3-Zero.
