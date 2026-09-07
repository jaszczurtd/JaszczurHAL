<a id="20---odbiornik-irsmalldecoder"></a>

# 20 - Odbiór poleceń z pilota na podczerwień

Przykład odbiera sygnał z odbiornika podczerwieni podłączonego do GPIO,
dekoduje ramki NEC i wypisuje je w konsoli diagnostycznej. Zmiany na wejściu
są obsługiwane przez przerwanie GPIO.

| Platforma | Wejście |
| --- | --- |
| RP2040 | GP16 |
| STM32G474 | PB0 |

Konsolę uruchamia `hal_debug_init_default()`. Do wypisywania danych i błędów
służą makra `deb` i `derr` z `hal/serial/hal_serial.h`.
Aby wybrać inny obsługiwany protokół, zmień `HAL_IRSMALL_PROTOCOL_NEC`
w konfiguracji dekodera w `app.c`.
