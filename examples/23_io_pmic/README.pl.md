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

Połączenia każdego montażu opisuje `device_tree.json`, a program bierze
magistrale, piny, adresy układów i zegar SPI rejestru 74HC595 (4 MHz)
z wygenerowanego nagłówka. I2C pracuje z częstotliwością 100 kHz, najwyższą,
jaką przyjmuje PCF8574. MCP3221 odpowiada pod adresem `0x4D`, MCP4725 pod
`0x60`, a ADP5360 pod `0x46`. Poniższa tabela powtarza te połączenia.

| Magistrala lub sygnał | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
| --- | --- | --- | --- |
| I2C SDA | GP4 | PB9, pin 5 CN10 / D14 | GPIO1 |
| I2C SCL | GP5 | PB8, pin 3 CN10 / D15 | GPIO2 |
| SPI MISO | GP16 | PA6, pin 13 CN10 / D12 | GPIO6 |
| SPI MOSI | GP19 | PA7, pin 15 CN10 / D11 | GPIO7 |
| SPI SCK | GP18 | PB3, pin 31 CN10 / D3 | GPIO5 |
| 74HC595 CS | GP17 | PB6, pin 17 CN10 / D10 | GPIO16 |
| Dane RGB | GP22 | PA8, pin 23 CN10 / D7 | GPIO8 |

Na NUCLEO-G474RE zegar SPI idzie przez PB3 zamiast D13 (PA5), bo PA5 steruje
diodą LD2. Na ESP32-S3-Zero magistrala I2C korzysta z GPIO1/GPIO2 zamiast
GPIO23/GPIO22, bo płytka nie ma tych pinów. Zastosuj zewnętrzne rezystory
podciągające linie I2C.
