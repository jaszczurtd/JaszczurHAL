<a id="19---kontrolery-dotyku-rezystancyjnego"></a>

# 19 - Odczyt dotyku: TSC2007 i STMPE610

Przykład odczytuje dane z rezystancyjnych kontrolerów dotyku TSC2007 i STMPE610
oraz wypisuje wyniki w konsoli diagnostycznej. Oba układy korzystają z I2C 0,
ale są inicjalizowane niezależnie.

| Platforma | SDA | SCL | TSC2007 | STMPE610 |
| --- | --- | --- | --- | --- |
| Rodzina RP | GP4 | GP5 | domyślnie `0x48` | domyślnie `0x41` |
| STM32G474 | PB9 | PB8 | domyślnie `0x48` | domyślnie `0x41` |

Zastosuj zewnętrzne rezystory podciągające linie I2C. Skompiluj projekt
zadaniem VS Code albo uruchom z głównego katalogu repozytorium:
`scripts/examples_dispatcher.py build --target rp2040 --example 19_touch`.
