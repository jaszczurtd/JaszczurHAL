<a id="20---odbiornik-irsmalldecoder"></a>

# 20 - Odbiór poleceń z pilota na podczerwień

Przykład odbiera sygnał z odbiornika podczerwieni podłączonego do GPIO,
dekoduje ramki NEC i wypisuje je w konsoli diagnostycznej. Zmiany na wejściu
są obsługiwane przez przerwanie GPIO. Sterownik włącza na wejściu rezystor
podciągający.

Połączenia każdego montażu opisuje `device_tree.json`, a program bierze pin
odbiornika z wygenerowanego nagłówka. Poniższa tabela powtarza te połączenia.

| Sygnał | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
| --- | --- | --- | --- |
| Wyjście odbiornika | GP16 | PB0, pin 34 CN7 / A3 | GPIO16 |

Konsolę uruchamia `hal_debug_init_default()`. Do wypisywania danych i błędów
służą makra `deb` i `derr` z `hal/serial/hal_serial.h`.
Aby wybrać inny obsługiwany protokół, zmień `HAL_IRSMALL_PROTOCOL_NEC`
w konfiguracji dekodera w `app.c`.
