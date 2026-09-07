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

| Sygnał | Rodzina RP | NUCLEO-G474RE |
|---|---|---|
| MCP9600 SDA / SCL | GP4 / GP5 | PB9 / PB8 |
| MAX6675 SCLK | GP18 | PA5, pin 11 CN10 / D13 |
| MAX6675 CS | GP17 | PB6, pin 17 CN10 / D10 |
| MAX6675 MISO | GP16 | PA6, pin 13 CN10 / D12 |

MCP9600 używa magistrali I2C 0, adresu `0x67` i standardowej częstotliwości
I2C. Aplikacja wybiera termoparę typu K i ustawia filtr na `2`.
Użyj termopar i modułów zgodnych z tymi ustawieniami; MAX6675 obsługuje typ K.

## Kompilacja

Z głównego katalogu repozytorium uruchom:

```bash
vscode/entry/jh-vscode build \
  --project examples/06_thermocouple --target rp2040 --board pico
```

Dostępne są również konfiguracje RP2350 ARM, RP2350 RISC-V i STM32G474.
