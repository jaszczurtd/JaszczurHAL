# 13 - ADC

Ten przykład odczytuje dwa wejścia wewnętrznego 12-bitowego ADC oraz wszystkie
cztery kanały ADS1115 pod adresem I2C `0x48`. Odczyty wejść wewnętrznych są
kontynuowane mimo braku zewnętrznego ADC, a inicjalizacja ADS1115 jest ponawiana
co pięć sekund.
Każdy kanał zewnętrzny sprawdza zarówno bezpośredni odczyt wartości ADC, jak i
odczyt po przeskalowaniu, który dodatkowo zwraca status operacji.

Targety RP używają GPIO 26/27 dla wewnętrznego ADC oraz GPIO 4/5 dla I2C
SDA/SCL. STM32G474 używa PA0/PA1 dla wewnętrznego ADC i PB9/PB8 dla I2C.
ADS1115 jest skonfigurowany na 0,1875 mV/LSB, czyli zakres ±6,144 V.
