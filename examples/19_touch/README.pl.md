# 19 - Kontrolery dotyku rezystancyjnego

Projekt sprawdza kontrolery TSC2007 i STMPE610. Oba współdzielą magistralę I2C
0 i są inicjalizowane niezależnie; próbki z każdego wykrytego urządzenia są
wypisywane przez konsolę debug.

| Target | SDA | SCL | TSC2007 | STMPE610 |
| --- | --- | --- | --- | --- |
| Rodzina RP | GP4 | GP5 | domyślnie `0x48` | domyślnie `0x41` |
| STM32G474 | PB9 | PB8 | domyślnie `0x48` | domyślnie `0x41` |

Wymagane są zewnętrzne rezystory podciągające I2C. Wykonaj build przez
wygenerowany manifest VS Code albo z głównego katalogu HAL poleceniem
`scripts/examples_dispatcher.py build --target rp2040 --example 19_touch`.
