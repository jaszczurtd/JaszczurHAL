<a id="05---porty-szeregowe-i-gps"></a>

# 05 - Odbiór danych GPS i test portu szeregowego

Przykład odczytuje dane GPS, a niezależnie od nich obsługuje drugi port
szeregowy do sprawdzania nadawania i odbioru. Brak odbiornika GPS lub połączenia
na drugim porcie nie zatrzymuje obsługi pozostałego urządzenia.

Wersja podstawowa korzysta ze sprzętowego UART. GPS pracuje z prędkością
9600 baud, a port testowy - 115200 baud.

| Platforma | GPS: port, RX / TX | Port testowy: port, RX / TX |
|---|---|---|
| Rodzina RP | UART 1, GP1 / GP0 | UART 2, GP5 / GP4 |
| STM32G474 | USART1, PA10 / PA9 | Niedostępny; USART2 na PA3 / PA2 służy konsoli diagnostycznej ST-Link VCP. |

Wariant `swserial` jest dostępny tylko dla rodziny RP i realizuje oba porty
programowo. GPS używa RX/TX na GP5/GP4, a port testowy na GP9/GP8.
Definicja `EXAMPLE_SERIAL_GPS_USE_SWSERIAL=1` wybiera programową obsługę
portu także dla modułu GPS; jest ustawiana w konfiguracji tego wariantu.

Aby sprawdzić pętlę zwrotną na RP, połącz TX portu testowego z jego RX.
Nie zwieraj w ten sposób linii portu podłączonego do odbiornika GPS.
