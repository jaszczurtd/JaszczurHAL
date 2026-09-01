# 15 - OLED i wyświetlacz znakowy LCD

Projekt sprawdza obie przenośne ścieżki wyświetlania:

- OLED SSD1306 128x64 przez I2C i wspólne buforowane API `hal_display`;
- zgodny z HD44780 LCD 16x2 w czterobitowym trybie GPIO (`RW` połączone z GND).

Urządzenia są inicjalizowane niezależnie, więc przykład działa także wtedy, gdy
jednego z nich nie ma na stanowisku. Każda kompilacja objęta bramką nadal
zawiera oba sterowniki.

| Sygnał | Rodzina RP | STM32G474 |
| --- | --- | --- |
| OLED SDA / SCL | GP4 / GP5 | PB9 / PB8 |
| LCD RS / E | GP12 / GP11 | PC0 / PC1 |
| LCD D4..D7 | GP10..GP7 | PC2..PC5 |

Zbuduj przez `../../vscode/entry/jh-vscode build --project . --target rp2040`
albo wybierz inny obsługiwany target z wygenerowanego manifestu projektu.
