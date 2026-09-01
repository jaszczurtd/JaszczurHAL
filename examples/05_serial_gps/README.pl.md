# 05 - Porty szeregowe i GPS

Ten przykład łączy parsowanie danych GPS z niezależnym echem portu szeregowego.

Domyślna aplikacja używa sprzętowego portu UART 1 dla GPS z prędkością 9600
baud. Na targetach RP używa też sprzętowego UART 2 dla echa 115200 baud: GPS
korzysta z RX/TX GPIO 1/0, a echo z GPIO 5/4. Na STM32G474 GPS używa USART1 na
PA10/PA9; USART2 na PA3/PA2 jest zarezerwowany dla debugowania przez ST-Link
VCP, dlatego drugie echo jest celowo dostępne tylko na RP.

Wariant `swserial`, dostępny tylko na RP, programowo obsługuje oba porty
szeregowe. GPS działa na RX/TX GPIO 5/4, a niezależny port z pętlą zwrotną i
echem na GPIO 9/8.
Zbuduj ten wariant z `EXAMPLE_SERIAL_GPS_USE_SWSERIAL=1`, aby backend GPS także
wybrał programową obsługę portu szeregowego.

Na targetach RP połącz pin TX każdego portu echa z odpowiadającym mu RX, aby
sprawdzić odbiór i nadawanie. Brak połączenia z GPS-em lub przerwana pętla echa
nie zatrzymują drugiej usługi.
