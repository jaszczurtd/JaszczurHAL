<a id="23---zewnętrzne-io-konwertery-pmic-i-led-rgb"></a>

# 23 - Dodatkowe wejścia i wyjścia, przetworniki i zasilanie

Przykład pokazuje obsługę ekspanderów GPIO MCP23017, PCA9654E i PCF8574,
rejestru wyjściowego 74HC595, przetworników ADC MCP3221 i DAC MCP4725,
układu zarządzania zasilaniem ADP5360 oraz pojedynczej adresowalnej diody RGB.
W przypadku ADP5360 obejmuje ładowarkę, odczyt stanu akumulatora i regulator
napięcia.

Urządzenia I2C są inicjalizowane niezależnie. Nie trzeba podłączać wszystkich
modułów, aby sprawdzić te, które są dostępne.

Ustaw piny adresowe ekspanderów tak, aby MCP23017 miał adres `0x20`,
PCA9654E - `0x21`, a PCF8574 - `0x22`. Te wartości odpowiadają konfiguracji
przykładu i zapobiegają konfliktom adresów.

| Magistrala lub sygnał | Rodzina RP | STM32G474 |
| --- | --- | --- |
| I2C SDA / SCL | GP4 / GP5 | PB9 / PB8 (D14 / D15) |
| SPI MISO / MOSI / SCK | GP16 / GP19 / GP18 | PA6 / PA7 / PA5 |
| 74HC595 CS | GP17 | PB6 |
| Dane RGB | GP22 | PA8 |

Na NUCLEO-G474RE sygnały SPI MISO/MOSI/SCK i `CS` rejestru 74HC595 znajdują
się odpowiednio na pinach 13/15/11/17 złącza CN10, czyli D12/D11/D13/D10.
Sprawdź adresy pozostałych modułów i zastosuj zewnętrzne rezystory
podciągające linie I2C.
