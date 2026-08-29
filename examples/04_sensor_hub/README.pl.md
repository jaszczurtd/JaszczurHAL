# 04 - Zestaw czujników

Ten przenośny przykład obsługuje w jednej pętli trzy niezależne czujniki:

- BH1750 pod adresem I2C `0x23`;
- DHT11 na linii danych GPIO;
- DS18B20 przez nieblokujący workflow OneWire.

Brak jednego czujnika jest raportowany bez zatrzymywania pozostałych. Targety
RP używają I2C GP4/GP5, DHT GP14 i DS18B20 GP16. STM32G474 używa I2C1 PB9/PB8,
DHT PA8 i DS18B20 PB0. Urządzenia I2C i OneWire wymagają zwykłych zewnętrznych
rezystorów podciągających.
