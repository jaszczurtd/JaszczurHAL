# 15 - OLED i wyświetlacz znakowy LCD

Przykład wyświetla dane na OLED SSD1306 128×64 oraz na znakowym LCD 16×2
zgodnym z HD44780. OLED korzysta z I2C i buforowanego API `hal_display`.
LCD pracuje w trybie czterobitowym przez GPIO; jego pin `RW` połącz z GND.

Oba wyświetlacze są inicjalizowane niezależnie. Możesz podłączyć tylko jeden
z nich, choć każda konfiguracja kompilacji obejmuje oba sterowniki.

| Sygnał | Rodzina RP | STM32G474 |
| --- | --- | --- |
| OLED SDA / SCL | GP4 / GP5 | PB9 / PB8 |
| LCD RS / E | GP12 / GP11 | PC0 / PC1 |
| LCD D4..D7 | GP10..GP7 | PC2..PC5 |

Uruchom `../../vscode/entry/jh-vscode build --project . --target rp2040`
z katalogu tego przykładu. Inne dostępne platformy są wymienione
w wygenerowanym pliku konfiguracji projektu.
