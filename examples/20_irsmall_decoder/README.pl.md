# 20 - Odbiornik IRsmallDecoder

Inicjalizuje dekoder odbiornika podczerwieni na pinie przerwania GPIO i wypisuje
zdekodowane ramki NEC przez helper debug.

Domyślne połączenia:

| Target | Wejście |
| --- | --- |
| RP2040 | GP16 |
| STM32G474 | PB0 |

Przykład używa `debugInit()` oraz makr logowania `deb` i `derr` z `tools_c.h`.
Zmień `HAL_IRSMALL_PROTOCOL_NEC` w `app.c`, aby wybrać inny wspierany protokół.
