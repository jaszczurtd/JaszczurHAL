<a id="19---kontrolery-dotyku-rezystancyjnego"></a>

# 19 - Odczyt dotyku: TSC2007 i STMPE610

Przykład odczytuje dane z rezystancyjnych kontrolerów dotyku TSC2007 i STMPE610
oraz wypisuje wyniki w konsoli diagnostycznej. Oba układy korzystają z I2C 0,
ale są inicjalizowane niezależnie.

Połączenia każdego montażu opisuje `device_tree.json`, a program bierze
magistralę I2C, piny, częstotliwość zegara (100 kHz) i adresy obu układów
z wygenerowanego nagłówka. TSC2007 ma swój domyślny adres `0x48`, a STMPE610
domyślny `0x41`. Poniższa tabela powtarza te połączenia.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
| --- | --- | --- | --- |
| I2C SDA | GP4 | PB9, pin 5 CN10 / D14 | GPIO4 |
| I2C SCL | GP5 | PB8, pin 3 CN10 / D15 | GPIO5 |

Na ESP32-S3-Zero magistrala I2C korzysta z GPIO4/GPIO5 zamiast GPIO25/GPIO24,
bo płytka nie ma tych pinów.

Zastosuj zewnętrzne rezystory podciągające linie I2C. Skompiluj projekt
zadaniem VS Code albo uruchom z głównego katalogu repozytorium:
`scripts/examples_dispatcher.py build --target rp2040 --example 19_touch`.
