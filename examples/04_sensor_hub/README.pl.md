<a id="04---zestaw-czujników"></a>

# 04 - Odczyt temperatury, wilgotności i oświetlenia

Przykład odczytuje natężenie oświetlenia z BH1750, temperaturę i wilgotność
z DHT11 oraz temperaturę z DS18B20. Obsługuje czujniki niezależnie: brak
jednego z nich jest zgłaszany w diagnostyce, ale nie zatrzymuje pozostałych.
Pomiar DS18B20 odbywa się bez blokowania pętli na czas konwersji.

BH1750 używa adresu I2C `0x23`. Podłącz czujniki zgodnie z tabelą:

| Sygnał | Rodzina RP | STM32G474 |
|---|---|---|
| BH1750 SDA / SCL | GP4 / GP5 | PB9 / PB8 (I2C1) |
| DHT11 DATA | GP14 | PA8 |
| DS18B20 DATA | GP16 | PB0 |

Linie I2C i OneWire wymagają zewnętrznych rezystorów podciągających do zasilania.
