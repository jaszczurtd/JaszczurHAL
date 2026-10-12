<a id="04---zestaw-czujników"></a>

# 04 - Odczyt temperatury, wilgotności i oświetlenia

Przykład odczytuje natężenie oświetlenia z BH1750, temperaturę i wilgotność
z DHT11 oraz temperaturę z DS18B20. Obsługuje czujniki niezależnie: brak
jednego z nich jest zgłaszany w diagnostyce, ale nie zatrzymuje pozostałych.
Pomiar DS18B20 odbywa się bez blokowania pętli na czas konwersji.

BH1750 używa adresu I2C `0x23`. Połączenia każdego montażu opisuje
`device_tree.json`, a program bierze magistralę, piny i adres czujnika
z wygenerowanego nagłówka. Poniższa tabela powtarza te połączenia.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| BH1750 SDA | GP4 | PB9 (I2C1), pin 5 CN10 / D14 | GPIO4 |
| BH1750 SCL | GP5 | PB8 (I2C1), pin 3 CN10 / D15 | GPIO5 |
| DHT11 DATA | GP14 | PA8, pin 23 CN10 / D7 | GPIO8 |
| DS18B20 DATA | GP16 | PB0, pin 34 CN7 / A3 | GPIO16 |

Na ESP32-S3-Zero magistrala I2C korzysta z GPIO4 / GPIO5, bo ESP32-S3 nie ma
GPIO24 ani GPIO25.

Linie I2C i OneWire wymagają zewnętrznych rezystorów podciągających do zasilania.
