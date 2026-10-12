# 15 - OLED i wyświetlacz znakowy LCD

Przykład wyświetla dane na OLED SSD1306 128×64 oraz na znakowym LCD 16×2
zgodnym z HD44780. OLED korzysta z I2C i buforowanego API `hal_display`.
LCD pracuje w trybie czterobitowym przez GPIO; jego pin `RW` połącz z GND.

Oba wyświetlacze są inicjalizowane niezależnie. Możesz podłączyć tylko jeden
z nich, choć każda konfiguracja kompilacji obejmuje oba sterowniki.

Połączenia każdego montażu opisuje `device_tree.json`, a program bierze
magistralę I2C, adres OLED (`0x3C`) i linie LCD z wygenerowanego nagłówka.
Poniższa tabela powtarza te połączenia. Magistrala I2C pracuje z
częstotliwością 400 kHz.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
| --- | --- | --- | --- |
| OLED SDA / SCL | GPIO4 / GPIO5 | PB9 / PB8 (D14 / D15) | GPIO4 / GPIO5 |
| LCD RS / E | GPIO12 / GPIO11 | PC0 / PC1 | GPIO12 / GPIO11 |
| LCD D4..D7 | GPIO10..GPIO7 | PC2..PC5 | GPIO10..GPIO7 |

Na ESP32-S3-Zero magistrala OLED korzysta z GPIO4/GPIO5 zamiast
GPIO25/GPIO24, których ESP32-S3 nie ma, a LCD z GPIO12..GPIO7 zamiast
GPIO32..GPIO37, niedostępnych na tej płytce.

Uruchom `../../vscode/entry/jh-vscode build --project . --assembly pico`
z katalogu tego przykładu. Pozostałe montaże to `pico2-arm`, `pico2-riscv`,
`nucleo` i `esp32s3-zero`.
