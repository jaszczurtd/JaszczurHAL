# 23 - Zewnętrzne I/O, konwertery, PMIC i LED RGB

Jeden build firmware sprawdza:

- ekspandery GPIO I2C MCP23017, PCA9654E i PCF8574;
- rejestr wyjściowy SPI 74HC595;
- ADC MCP3221 i DAC MCP4725;
- ładowarkę, miernik poziomu baterii i regulator ADP5360;
- jednokanałowy adresowalny LED RGB.

Każde urządzenie I2C jest inicjalizowane niezależnie, dlatego niepełne
stanowisko nadal sprawdza obecne drivery.

Trzy ekspandery GPIO, których adresy domyślnie by kolidowały, używają jawnie
ustawionych adresów: MCP23017 `0x20`, PCA9654E `0x21` i PCF8574 `0x22`.
Skonfiguruj odpowiednio piny adresowe każdego modułu.

| Magistrala lub sygnał | Rodzina RP | STM32G474 |
| --- | --- | --- |
| I2C SDA / SCL | GP4 / GP5 | PB9 / PB8 (D14 / D15) |
| SPI MISO / MOSI / SCK | GP16 / GP19 / GP18 | PA6 / PA7 / PA5 |
| 74HC595 CS | GP17 | PB6 |
| Dane RGB | GP22 | PA8 |

Na NUCLEO-G474RE sygnały SPI MISO/MOSI/SCK i CS 74HC595 są dostępne na pinach
13/15/11/17 CN10, odpowiadających D12/D11/D13/D10.

Ustaw odpowiednie adresy I2C i zastosuj zewnętrzne rezystory podciągające dla
podłączonych modułów.
