# 05 - Serial i GPS

Ten przykład łączy analizowanie danych GPS z niezależną ścieżką echo serial.

Domyślna aplikacja używa sprzętowego portu UART 1 dla GPS z prędkością 9600
baud. Na targetach RP używa też sprzętowego UART 2 dla echo 115200 baud: GPS
korzysta z RX/TX GPIO 1/0, a echo z GPIO 5/4. Na STM32G474 GPS używa USART1 na
PA10/PA9; USART2 na PA3/PA2 pozostaje wyłączną własnością debug/ST-Link VCP,
dlatego drugie echo jest celowo dostępne tylko na RP.

Wariant `swserial`, dostępny tylko na RP, używa software serial dla obu ścieżek.
GPS działa na RX/TX GPIO 5/4, a niezależny port loopback/echo na GPIO 9/8.
Zbuduj ten wariant z `EXAMPLE_SERIAL_GPS_USE_SWSERIAL=1`, aby backend GPS także
wybrał software serial.

Na targetach RP połącz pin TX każdego echo z odpowiadającym mu RX, aby sprawdzić
odbiór i nadawanie. Odłączony GPS lub loop echo nie zatrzymuje drugiej usługi.
