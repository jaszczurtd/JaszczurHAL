# 20 - Odbiornik IRsmallDecoder

Inicjalizuje dekoder odbiornika podczerwieni na pinie przerwania GPIO i wypisuje
zdekodowane ramki NEC za pomocą funkcji diagnostycznej.

Domyślne połączenia:

| Target | Wejście |
| --- | --- |
| RP2040 | GP16 |
| STM32G474 | PB0 |

Przykład używa `hal_debug_init_default()` oraz wspieranych makr logowania
`deb` i `derr` z `hal/serial/hal_serial.h`.
Zmień `HAL_IRSMALL_PROTOCOL_NEC` w `app.c`, aby wybrać inny obsługiwany protokół.
