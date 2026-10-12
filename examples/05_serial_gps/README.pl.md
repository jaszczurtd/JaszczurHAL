<a id="05---porty-szeregowe-i-gps"></a>

# 05 - Odbiór danych GPS i test portu szeregowego

Przykład odczytuje dane GPS, a niezależnie od nich obsługuje drugi port
szeregowy do sprawdzania nadawania i odbioru. Brak odbiornika GPS lub połączenia
na drugim porcie nie zatrzymuje obsługi pozostałego urządzenia.

Wersja podstawowa korzysta ze sprzętowego UART. GPS pracuje z prędkością
9600 baud, a port testowy - 115200 baud. Połączenia każdego montażu opisuje
`device_tree.json`, a program bierze porty, piny i ustawienia transmisji
z wygenerowanego nagłówka. Poniższa tabela powtarza te połączenia.

| Montaż | GPS: port, RX / TX | Port testowy: port, RX / TX |
|---|---|---|
| Pico / Pico 2 | UART 1, GP1 / GP0 | UART 2, GP5 / GP4 |
| NUCLEO-G474RE | USART1, PA10 / PA9 | Niedostępny; USART2 na PA3 / PA2 służy konsoli diagnostycznej ST-Link VCP. |
| ESP32-S3-Zero | UART 1, GPIO10 / GPIO9 | Niedostępny |

Wariant `SWSERIAL` jest dostępny tylko dla rodziny RP i realizuje oba porty
programowo na tych samych liniach: GPS na GP1/GP0, a port testowy na GP5/GP4,
z prędkością 9600 baud. Obie wersje działają więc na jednym, niezmienionym
stanowisku.
Definicja `EXAMPLE_SERIAL_GPS_USE_SWSERIAL=1` wybiera programową obsługę
portu także dla modułu GPS; jest ustawiana w konfiguracji tego wariantu.

Aby sprawdzić pętlę zwrotną na RP, połącz TX portu testowego z jego RX.
Nie zwieraj w ten sposób linii portu podłączonego do odbiornika GPS.
